/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include <sys/sysinfo.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <syslog.h>
#include <stdarg.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>

#include <ralink_priv.h>
#include <flash_mtd.h>

#include "rc.h"

#define XSTR(s) STR(s)
#define STR(s) #s

long
uptime(void)
{
	struct sysinfo info;
	sysinfo(&info);

	return info.uptime;
}

int
rand_seed_by_time(void)
{
	time_t atime;

	/* make a random number and set the top and bottom bits */
	time(&atime);
	srand((unsigned long)atime);

	return rand();
}

/* convert mac address format from XXXXXXXXXXXX to XX:XX:XX:XX:XX:XX */
char *
mac_conv(const char *mac_nvkey, int idx, char *buf)
{
	char *mac, name[32];
	int i, j;

	if (idx < 0)
		snprintf(name, sizeof(name), "%s", mac_nvkey);
	else
		snprintf(name, sizeof(name), "%s%d", mac_nvkey, idx);

	mac = nvram_safe_get(name);

	buf[0] = 0;
	if (strlen(mac) == 12) {
		for (i = 0, j = 0; i < 12; i++) {
			if (i != 0 && (i%2) == 0)
				buf[j++] = ':';
			buf[j++] = mac[i];
		}
		buf[j] = 0;	// oleg patch
	}

	if (strcasecmp(buf, "FF:FF:FF:FF:FF:FF") == 0 || strcmp(buf, "00:00:00:00:00:00") == 0)
		buf[0] = 0;

	return (buf);
}

/* convert mac address format from XX:XX:XX:XX:XX:XX to XXXXXXXXXXXX */
char *
mac_conv2(const char *mac_nvkey, int idx, char *buf)
{
	char *mac, name[32];
	int i, j;

	if (idx < 0)
		snprintf(name, sizeof(name), "%s", mac_nvkey);
	else
		snprintf(name, sizeof(name), "%s%d", mac_nvkey, idx);

	mac = nvram_safe_get(name);

	buf[0] = 0;
	if (strlen(mac) == 17 && strcasecmp(buf, "FF:FF:FF:FF:FF:FF") && strcmp(buf, "00:00:00:00:00:00")) {
		for(i = 0, j = 0; i < 17; ++i) {
			if (i%3 != 2){
				buf[j] = mac[i];
				++j;
			}
			buf[j] = 0;
		}
	}

	return (buf);
}

int
valid_subver(char subfs)
{
	printf("validate subfs: %c\n", subfs);	// tmp test
	if(((subfs >= 'a') && (subfs <= 'z' )) || ((subfs >= 'A') && (subfs <= 'Z' )))
		return 1;
	else
		return 0;
}

void
get_eeprom_params(void)
{
#if defined (VENDOR_ASUS)
	int i;
#endif
	int i_offset, i_ret;
	unsigned char buffer[32];
	unsigned char ea[ETHER_ADDR_LEN];
	char macaddr_wl[]  = "00:11:22:33:44:55";
	char macaddr_rt[]  = "00:11:22:33:44:56";
	char macaddr_lan[] = "00:11:22:33:44:55";
	char macaddr_wan[] = "00:11:22:33:44:56";
	char country_code[4];
	char regspec_code[8];
	char wps_pin[12];
	char productid[16];
	char fwver[8], fwver_sub[32];

#if (BOARD_5G_IN_SOC || !BOARD_HAS_5G_RADIO)
	i_offset = OFFSET_MAC_ADDR_WSOC;
#else
	i_offset = OFFSET_MAC_ADDR_INIC;
#endif
	memset(buffer, 0xff, ETHER_ADDR_LEN);
	i_ret = flash_mtd_read(MTD_PART_NAME_FACTORY, i_offset, buffer, ETHER_ADDR_LEN);
	if (i_ret >= 0 && !(buffer[0] & 0x01))
		ether_etoa(buffer, macaddr_wl);

#if BOARD_2G_AS_WSOC
	i_offset = OFFSET_MAC_ADDR_WSOC;
#else
	i_offset = OFFSET_MAC_ADDR_INIC;
#endif
	memset(buffer, 0xff, ETHER_ADDR_LEN);
	i_ret = flash_mtd_read(MTD_PART_NAME_FACTORY, i_offset, buffer, ETHER_ADDR_LEN);
	if (i_ret >= 0 && !(buffer[0] & 0x01))
		ether_etoa(buffer, macaddr_rt);

	i_offset = get_wired_mac_e2p_offset(0);
	memset(buffer, 0xff, ETHER_ADDR_LEN);
	i_ret = flash_mtd_read(MTD_PART_NAME_FACTORY, i_offset, buffer, ETHER_ADDR_LEN);
	if (buffer[0] & 0x01) {
		if (ether_atoe(macaddr_wl, ea)) {
			memcpy(buffer, ea, ETHER_ADDR_LEN);
			strcpy(macaddr_lan, macaddr_wl);
			if (i_ret >= 0)
				flash_mtd_write(MTD_PART_NAME_FACTORY, i_offset, ea, ETHER_ADDR_LEN);
		}
	} else {
		ether_etoa(buffer, macaddr_lan);
	}

	/* store wired LAN MAC */
	memcpy(buffer+ETHER_ADDR_LEN, buffer, ETHER_ADDR_LEN);

	if (get_wired_mac_is_single()) {
		buffer[5] |= 0x03;	// last 2 bits reserved for MBSSID, use 0x03 for WAN (ra1: 0x01, apcli0: 0x02)
		ether_etoa(buffer, macaddr_wan);
	} else {
		i_offset = get_wired_mac_e2p_offset(1);
		memset(buffer, 0xff, ETHER_ADDR_LEN);
		i_ret = flash_mtd_read(MTD_PART_NAME_FACTORY, i_offset, buffer, ETHER_ADDR_LEN);
		if ((buffer[0] & 0x01)
#if defined (USE_SINGLE_MAC)
		    /* compare LAN/WAN MAC OID */
		    || memcmp(buffer, buffer+ETHER_ADDR_LEN, 3) != 0
#endif
		    ) {
			memcpy(buffer, buffer+ETHER_ADDR_LEN, ETHER_ADDR_LEN);
			buffer[5] |= 0x03;
			ether_etoa(buffer, macaddr_wan);
#if !defined (USE_SINGLE_MAC)
			if (i_ret >= 0)
				flash_mtd_write(MTD_PART_NAME_FACTORY, i_offset, buffer, ETHER_ADDR_LEN);
#endif
		} else {
			ether_etoa(buffer, macaddr_wan);
		}
	}

	nvram_set_temp("il0macaddr", macaddr_lan); // LAN
	nvram_set_temp("il1macaddr", macaddr_wan); // WAN
	nvram_set_temp("wl_macaddr", macaddr_wl);  // 5 GHz
	nvram_set_temp("rt_macaddr", macaddr_rt);  // 2.4 GHZ

#if defined (VENDOR_ASUS)
	/* reserved for Ralink. used as ASUS country code. */
	memset(country_code, 0, sizeof(country_code));
	i_ret = flash_mtd_read(MTD_PART_NAME_FACTORY, OFFSET_COUNTRY_CODE, country_code, 2);
	if (i_ret < 0) {
		strcpy(country_code, "GB");
	} else {
		country_code[2] = 0;
		for (i = 1; i >= 0; i--) {
			if ((unsigned char)country_code[i] > 0x7f)
				country_code[i] = 0;
		}
		if (country_code[0] == 0)
			strcpy(country_code, "GB");
	}
#else
	strcpy(country_code, "GB");
#endif

	if (strlen(nvram_wlan_get(0, "country_code")) == 0)
		nvram_wlan_set(0, "country_code", country_code);

	if (strlen(nvram_wlan_get(1, "country_code")) == 0)
		nvram_wlan_set(1, "country_code", country_code);

#if defined (VENDOR_ASUS)
	/* reserved for Ralink. used as ASUS RegSpec code. */
	memset(regspec_code, 0, sizeof(regspec_code));
	i_ret = flash_mtd_read(MTD_PART_NAME_FACTORY, OFFSET_REGSPEC_CODE, regspec_code, 4);
	if (i_ret < 0) {
		strcpy(regspec_code, "CE");
	} else {
		regspec_code[4] = 0;
		for (i = 3; i >= 0; i--) {
			if ((unsigned char)regspec_code[i] > 0x7f)
				regspec_code[i] = 0;
		}
		
		if (!check_regspec_code(regspec_code))
			strcpy(regspec_code, "CE");
	}
#else
	strcpy(regspec_code, "CE");
#endif
	nvram_set_temp("regspec_code", regspec_code);

#if defined (VENDOR_ASUS)
	/* reserved for Ralink. used as ASUS pin code. */
	memset(wps_pin, 0, sizeof(wps_pin));
	i_ret = flash_mtd_read(MTD_PART_NAME_FACTORY, OFFSET_PIN_CODE, wps_pin, 8);
	if (i_ret < 0) {
		strcpy(wps_pin, "12345670");
	} else {
		wps_pin[8] = 0;
		for (i = 7; i >= 0; i--) {
			if ((unsigned char)wps_pin[i] < 0x30 ||
			    (unsigned char)wps_pin[i] > 0x39)
				wps_pin[i] = 0;
		}
		if (wps_pin[0] == 0)
			strcpy(wps_pin, "12345670");
	}
#else
	strcpy(wps_pin, "12345670");
#endif
	nvram_set_temp("secret_code", wps_pin);

#if defined(USE_RT3352_MII)
 #define EEPROM_INIC_SIZE (512)
 #define EEPROM_INIC_ADDR 0x8000
	{
		char eeprom[EEPROM_INIC_SIZE];
		i_ret = flash_mtd_read(MTD_PART_NAME_FACTORY, EEPROM_INIC_ADDR, eeprom, sizeof(eeprom));
		if (i_ret < 0) {
			dbg("READ iNIC EEPROM: Out of scope!\n");
		} else {
			FILE *fp;
			if((fp = fopen("/etc/Wireless/iNIC/iNIC_e2p.bin", "w"))) {
				fwrite(eeprom, sizeof(eeprom), 1, fp);
				fclose(fp);
			}
		}
	}
#endif

	/* read firmware header */
	strcpy(fwver, "3.0.0.0");
	strcpy(fwver_sub, fwver);
	snprintf(productid, sizeof(productid), "%s", BOARD_PID);
	memset(buffer, 0, sizeof(buffer));
	i_ret = flash_mtd_read(MTD_PART_NAME_KERNEL, 0x20, buffer, 32);
	if (i_ret < 0) {
		nvram_set_temp("productid", "unknown");
		nvram_set_temp("firmver", "unknown");
	} else {
		strncpy(productid, buffer + 4, 12);
		productid[12] = 0;
		
		if(valid_subver(buffer[27]))
			sprintf(fwver_sub, "%d.%d.%d.%d%c", buffer[0], buffer[1], buffer[2], buffer[3], buffer[27]);
		else
			sprintf(fwver_sub, "%d.%d.%d.%d", buffer[0], buffer[1], buffer[2], buffer[3]);
		
		sprintf(fwver, "%d.%d.%d.%d", buffer[0], buffer[1], buffer[2], buffer[3]);
	}

#if defined(FWBLDSTR)
	if (strlen(FWBLDSTR) > 0 && strlen(FWBLDSTR) <= 4) {
		strcat(fwver_sub, "-");
		strcat(fwver_sub, FWBLDSTR);
	}
#endif
#if defined(FWREVSTR)
	if (strlen(FWREVSTR) > 0 && strlen(FWREVSTR) <= 8) {
		strcat(fwver_sub, "_");
		strcat(fwver_sub, FWREVSTR);
	}
#endif
	nvram_set_temp("productid", trim_r(productid));
	nvram_set_temp("firmver", trim_r(fwver));
	nvram_set_temp("firmver_sub", trim_r(fwver_sub));

#if 0
#if defined (VENDOR_ASUS)
	memset(buffer, 0, 4);
	i_ret = flash_mtd_read(MTD_PART_NAME_FACTORY, OFFSET_BOOT_VER, buffer, 4);
	if (i_ret == 0) {
		char blver[32];
		snprintf(blver, sizeof(blver), "%s-0%c-0%c-0%c-0%c", trim_r(productid), buffer[0], buffer[1], buffer[2], buffer[3]);
		nvram_set_temp("blver", trim_r(blver));
	}
#endif
#endif

#if 0
	// TXBF, not used yet
	{
		int i, count_0xff = 0;
		unsigned char txbf_para[33];
		memset(txbf_para, 0, sizeof(txbf_para));
		i_ret = flash_mtd_read(MTD_PART_NAME_FACTORY, OFFSET_TXBF_PARA, txbf_para, 33);
		if (i_ret < 0) {
			dbg("READ TXBF PARA address: Out of scope\n");
		} else {
			for (i = 0; i < 33; i++) {
				if (txbf_para[i] == 0xff)
					count_0xff++;
			}
		}
		
		nvram_wlan_set_int(1, "txbf_en", (count_0xff == 33) ? 0 : 1);
	}
#endif
}

void
update_router_mode(void)
{
	if (nvram_get_int("sw_mode") != 3) {
		if (nvram_get_int("wan_nat_x") == 0)
			nvram_set_int("sw_mode", 4);	// Gateway mode
		else
			nvram_set_int("sw_mode", 1);	// Router mode
	}
}

void
set_pagecache_reclaim(void)
{
	int pagecache_ratio = 100;
	int pagecache_reclaim = nvram_get_int("pcache_reclaim");

	switch (pagecache_reclaim)
	{
	case 1:
		pagecache_ratio = 30;
		break;
	case 2:
		pagecache_ratio = 50;
		break;
	case 3:
		pagecache_ratio = 70;
		break;
	case 4:
		pagecache_ratio = 85;
		break;
	}

	fput_int("/proc/sys/vm/pagecache_ratio", pagecache_ratio);
}

void
restart_all_sysctl(void)
{
	if (!get_ap_mode()) {
		set_nf_conntrack();
		set_tcp_syncookies();
		set_igmp_mld_version();
		set_passthrough_pppoe(1);
	}

#if defined(APP_SMBD)
	config_smb_fastpath(1);
#endif
}


/**
 * hex char to int
 * @author 荒野无灯
 * @date 2016-08-01 12:30
 * @param hex
 * @return int
 */
static int hex_char_to_int(const uint8_t *hex)
{
        int val;
        if(*hex >= '0' && *hex <='9') {
                val = (*hex - '0') * 16;
        } else {
                val = (*hex - 'A' + 10) * 16;
        }

        if(*(hex+1) >= '0' && *(hex+1) <='9') {
                val += (*(hex+1) - '0');
        } else {
                val += (*(hex+1) - 'A' +10);
        }
        return val;
}

/**
 * @author 荒野无灯
 * @date 2016-08-01 12:30
 * @param str the str to detect
 * @param sz string total bytes
 * @return
 */
static int can_be_chinese_utf8(const uint8_t *str, int sz)
{
        int len = strlen (str);
        if (sz < 6) {
                return 0;
        }

        if ((len >= 6) &&
                (hex_char_to_int(str) >= 0xe4 && hex_char_to_int(str) <= 0xe9)
                && (hex_char_to_int(str+2) >= 0x80 && hex_char_to_int(str+2) <= 0xbf)
                && (hex_char_to_int(str+4) >= 0x80 && hex_char_to_int(str+4) <= 0xbf)
                        ) {
                return 1;
        }
        if ( ((sz - len >= 2) && (len >=4)) &&
                 (hex_char_to_int(str-2) >= 0xe4 && hex_char_to_int(str-2) <= 0xe9)
                 && (hex_char_to_int(str) >= 0x80 && hex_char_to_int(str) <= 0xbf)
                 && (hex_char_to_int(str+2) >= 0x80 && hex_char_to_int(str+2) <= 0xbf)
                        ) {
                return 1;
        }
        if (((sz - len >= 4) && (len >= 2))
                && (hex_char_to_int(str-4) >= 0xe4 && hex_char_to_int(str-4) <= 0xe9 )
                && ((hex_char_to_int(str-2) >= 0x80 && hex_char_to_int(str-2) <= 0xbf)
                        || (hex_char_to_int(str) >= 0x80 && hex_char_to_int(str) <= 0xbf))) {
                return 1;
        }
        return 0;
}

/**
 * @author 荒野无灯
 * @date 2016-08-01 12:30
 * @param str the str to detect
 * @param sz string total bytes
 * @return int
 */
static int can_be_ascii_utf8(const uint8_t *str, int sz)
{
        int len = strlen (str);
        uint8_t the_char = hex_char_to_int(str);
        /*printf("ascii detect:  ascii_val: %d, canbe char: %c\n", the_char, the_char);*/
        if (( len >= 2) &&
                (the_char >= '0' && the_char <= '9')
                || (the_char >= 'A' && the_char <= 'Z')
                || (the_char >= 'a' && the_char <= 'z')
                || the_char == '!' || the_char == '*'
                || the_char == '(' || the_char == ')'
                || the_char == '_' || the_char == '-'
                || the_char == '\'' || the_char == '.') {
                return 1;
        }
        return 0;
}

/**
 * @param input the string to validate
 * @author 荒野无灯
 * @date 2016-08-01 12:30
 * @return int
 */
static int is_valid_hex_string(uint8_t *input)
{
        int i;
        int input_len, input_hex_len;
        char *input_ptr;

        //detect from index 2, skip char "0x"
        input_ptr = input+2;
        input_len = strlen(input);
        input_hex_len = input_len -2;
        int is_valid_ascii_or_Chinese = 1;
        //0xAA
        if(input_len >4 && input_len % 2 == 0 && input[0] == '0' && input[1] == 'x') {
                for ( i=0; i<input_hex_len; i+=2, input_ptr+=2 ) {
                        if (!( ((*input_ptr>='0' && *input_ptr <='9') || ( *input_ptr >='A' && *input_ptr <='F'))
                                   && ((*(input_ptr+1) >='0' && *(input_ptr+1) <='9') || ( *(input_ptr+1) >='A' && *(input_ptr+1) <='F')))
                                        ) {
                                is_valid_ascii_or_Chinese = 0;
                                break;
                        }
                        if (!can_be_chinese_utf8(input_ptr, input_hex_len) && !can_be_ascii_utf8(input_ptr, input_hex_len)) {
                                is_valid_ascii_or_Chinese = 0;
                                break;
                        }
                }
        } else {
                is_valid_ascii_or_Chinese = 0;
        }
        return is_valid_ascii_or_Chinese;
}

void
char_to_ascii(char *output, uint8_t *input)
{
        int i;
        char tmp[10];
        char *ptr;
        int input_len;

        ptr = output;
        input_len = strlen(input);

        if(is_valid_hex_string(input)) {
                for ( i=2; i<input_len; i +=2 ) {
                        sprintf(tmp, "%%%c%c", input[i], input[i+1]);
                        strcpy(ptr, tmp);
                        ptr+=3;
                }
        }  else {
                for ( i=0; i<input_len; i++ ) {
                        if ((input[i]>='0' && input[i] <='9')
                                ||(input[i]>='A' && input[i]<='Z')
                                ||(input[i] >='a' && input[i]<='z')
                                || input[i] == '!' || input[i] == '*'
                                || input[i] == '(' || input[i] == ')'
                                || input[i] == '_' || input[i] == '-'
                                || input[i] == '\'' || input[i] == '.') {
                                *ptr = input[i];
                                ptr++;
                        } else {
                                sprintf(tmp, "%%%.02X", input[i]);
                                strcpy(ptr, tmp);
                                ptr += 3;
                        }
                }
        }
        *ptr = '\0';
}
int
fput_string(const char *name, const char *value)
{
	FILE *fp;

	fp = fopen(name, "w");
	if (fp) {
		fputs(value, fp);
		fclose(fp);
		return 0;
	} else {
		return errno;
	}
}

int
fput_int(const char *name, int value)
{
	char svalue[32];
	sprintf(svalue, "%d", value);
	return fput_string(name, svalue);
}

unsigned int
get_param_int_hex(const char *param)
{
	unsigned int retVal = 0;

	/* check HEX */
	if (strlen(param) > 2 && param[0] == '0' && (param[1] == 'x' || param[1] == 'X'))
		retVal = strtoul(param+2, NULL, 16);
	else
		retVal = atoi(param);

	return retVal;
}

static int
is_param_forbidden(const char *line, const char **forbid_list)
{
	while (*forbid_list) {
		if (strncmp(line, *forbid_list, strlen(*forbid_list)) == 0)
			return 1;
		forbid_list++;
	}
	return 0;
}

void
load_user_config(FILE *fp, const char *dir_name, const char *file_name, const char **forbid_list)
{
	FILE *fp_user;
	char line[256], real_path[128];

	snprintf(real_path, sizeof(real_path), "%s/%s", dir_name, file_name);
	fp_user = fopen(real_path, "r");
	if (fp_user) {
		while (fgets(line, sizeof(line), fp_user)) {
			if (line[0] == '\0' ||
			    line[0] == '\r' ||
			    line[0] == '\n' ||
			    line[0] == '#' ||
			    line[0] == ';')
				continue;
			
			if (forbid_list && is_param_forbidden(line, forbid_list))
				continue;
			
			line[strlen(line) - 1] = '\n';
			fprintf(fp, line);
		}
		
		fclose(fp_user);
	}
}

int
is_module_loaded(const char *module_name)
{
	DIR *dir_to_open = NULL;
	char mod_path[64];

	snprintf(mod_path, sizeof(mod_path), "/sys/module/%s", module_name);
	dir_to_open = opendir(mod_path);
	if (dir_to_open) {
		closedir(dir_to_open);
		return 1;
	}

	return 0;
}

int
get_module_refcount(const char *module_name)
{
	FILE *fp;
	char mod_path[64], mod_refval[16];
	int refcount = 0;

	snprintf(mod_path, sizeof(mod_path), "/sys/module/%s/refcnt", module_name);
	fp = fopen(mod_path, "r");
	if (!fp)
		return -1;

	mod_refval[0] = 0;
	fgets(mod_refval, sizeof(mod_refval), fp);
	if (strlen(mod_refval) > 0)
		refcount = atoi(mod_refval);

	fclose(fp);

	return refcount;
}

int
module_smart_load(const char *module_name, const char *module_param)
{
	int ret;

	if (is_module_loaded(module_name))
		return 0;

	if (module_param && *module_param)
		ret = doSystem("modprobe -q %s %s", module_name, module_param);
	else
		ret = doSystem("modprobe -q %s", module_name);

	return (ret == 0) ? 1 : 0;
}

int
module_smart_unload(const char *module_name, int recurse_unload)
{
	int ret;
	int refcount = get_module_refcount(module_name);

	/* check module not loaded */
	if (refcount < 0)
		return 0;

	/* check module is used */
	if (refcount > 0)
		return 1;

	if (recurse_unload)
		ret = doSystem("modprobe -r %s", module_name);
	else
		ret = doSystem("rmmod %s", module_name);

	return (ret == 0) ? 1 : 0;
}

int
module_param_get(const char *module_name, const char *module_param, char *param_value, size_t param_value_size)
{
	FILE *fp;
	char mod_path[256];

	if (!param_value || param_value_size < 2)
		return -1;

	snprintf(mod_path, sizeof(mod_path), "/sys/module/%s/parameters/%s", module_name, module_param);
	fp = fopen(mod_path, "r");
	if (!fp)
		return -1;

	param_value[0] = 0;
	fgets(param_value, param_value_size, fp);
	if (strlen(param_value) > 0)
		param_value[strlen(param_value) - 1] = 0; /* get rid of '\n' */

	fclose(fp);

	return 0;
}

int
module_param_set_int(const char *module_name, const char *module_param, int param_value)
{
	char mod_path[256];

	snprintf(mod_path, sizeof(mod_path), "/sys/module/%s/parameters/%s", module_name, module_param);
	fput_int(mod_path, param_value);

	return 0;
}

void
oom_score_adjust(pid_t pid, int oom_score_adj)
{
	char proc_path[40];

	snprintf(proc_path, sizeof(proc_path), "/proc/%u/oom_score_adj", pid);
	fput_int(proc_path, oom_score_adj);
}

#if defined (CONFIG_RALINK_MT7621)
static void
irq_affinity_set(int irq_num, int cpu)
{
	char proc_path[40];

	snprintf(proc_path, sizeof(proc_path), "/proc/irq/%d/smp_affinity", irq_num);
	fput_int(proc_path, cpu);
}

static void
rps_queue_set(const char *ifname, int cpu_mask)
{
	char proc_path[64], cmx[4];

	snprintf(proc_path, sizeof(proc_path), "/sys/class/net/%s/queues/rx-%d/rps_cpus", ifname, 0);
	snprintf(cmx, sizeof(cmx), "%x", cpu_mask);
	fput_string(proc_path, cmx);
}

static void
xps_queue_set(const char *ifname, int cpu_mask)
{
	char proc_path[64], cmx[4];

	snprintf(proc_path, sizeof(proc_path), "/sys/class/net/%s/queues/tx-%d/xps_cpus", ifname, 0);
	snprintf(cmx, sizeof(cmx), "%x", cpu_mask);
	fput_string(proc_path, cmx);
}

void
set_cpu_affinity(int is_ap_mode)
{
	/* set initial IRQ affinity and RPS/XPS balancing */
	int ncpu = sysconf(_SC_NPROCESSORS_ONLN);

#define GIC_IRQ_FE	(GIC_OFFSET+3)
#define GIC_IRQ_PCIE0	(GIC_OFFSET+4)
#define GIC_IRQ_PCIE1	(GIC_OFFSET+24)
#define GIC_IRQ_PCIE2	(GIC_OFFSET+25)
#define GIC_IRQ_SDXC	(GIC_OFFSET+20)
#define GIC_IRQ_XHCI	(GIC_OFFSET+22)
#define GIC_IRQ_EIP93	(GIC_OFFSET+19)

	if (ncpu == 4) {
		irq_affinity_set(GIC_IRQ_FE,    2);	/* GMAC  -> CPU:0, VPE:1 */
		irq_affinity_set(GIC_IRQ_PCIE0, 4);	/* PCIe0 -> CPU:1, VPE:0 (usually rai0) */
		irq_affinity_set(GIC_IRQ_PCIE1, 8);	/* PCIe1 -> CPU:1, VPE:1 (usually ra0) */
		irq_affinity_set(GIC_IRQ_PCIE2, 1);	/* PCIe2 -> CPU:0, VPE:0 (usually ahci) */
		irq_affinity_set(GIC_IRQ_SDXC,  4);	/* SDXC  -> CPU:1, VPE:0 */
		irq_affinity_set(GIC_IRQ_XHCI,  8);	/* xHCI  -> CPU:1, VPE:1 */
		irq_affinity_set(GIC_IRQ_EIP93, 8);	/* EIP93 -> CPU:1, VPE:1 */
		
		rps_queue_set(IFNAME_2G_MAIN,  0x8);	/* CPU:1, VPE:1 (PCIe1) */
		xps_queue_set(IFNAME_2G_MAIN,  0x8);	/* CPU:1, VPE:1 (PCIe1) */
		rps_queue_set(IFNAME_2G_GUEST, 0x8);	/* CPU:1, VPE:1 (PCIe1) */
		xps_queue_set(IFNAME_2G_GUEST, 0x8);	/* CPU:1, VPE:1 (PCIe1) */
		rps_queue_set(IFNAME_2G_APCLI, 0x8);	/* CPU:1, VPE:1 (PCIe1) */
		xps_queue_set(IFNAME_2G_APCLI, 0x8);	/* CPU:1, VPE:1 (PCIe1) */
		rps_queue_set(IFNAME_2G_WDS0,  0x8);	/* CPU:1, VPE:1 (PCIe1) */
		xps_queue_set(IFNAME_2G_WDS0,  0x8);	/* CPU:1, VPE:1 (PCIe1) */
		rps_queue_set(IFNAME_2G_WDS1,  0x8);	/* CPU:1, VPE:1 (PCIe1) */
		xps_queue_set(IFNAME_2G_WDS1,  0x8);	/* CPU:1, VPE:1 (PCIe1) */
		rps_queue_set(IFNAME_2G_WDS2,  0x8);	/* CPU:1, VPE:1 (PCIe1) */
		xps_queue_set(IFNAME_2G_WDS2,  0x8);	/* CPU:1, VPE:1 (PCIe1) */
		rps_queue_set(IFNAME_2G_WDS3,  0x8);	/* CPU:1, VPE:1 (PCIe1) */
		xps_queue_set(IFNAME_2G_WDS3,  0x8);	/* CPU:1, VPE:1 (PCIe1) */
#if BOARD_HAS_5G_RADIO
		rps_queue_set(IFNAME_5G_MAIN,  0x4);	/* CPU:1, VPE:0 (PCIe0) */
		xps_queue_set(IFNAME_5G_MAIN,  0x4);	/* CPU:1, VPE:0 (PCIe0) */
		rps_queue_set(IFNAME_5G_GUEST, 0x4);	/* CPU:1, VPE:0 (PCIe0) */
		xps_queue_set(IFNAME_5G_GUEST, 0x4);	/* CPU:1, VPE:0 (PCIe0) */
		rps_queue_set(IFNAME_5G_APCLI, 0x4);	/* CPU:1, VPE:0 (PCIe0) */
		xps_queue_set(IFNAME_5G_APCLI, 0x4);	/* CPU:1, VPE:0 (PCIe0) */
		rps_queue_set(IFNAME_5G_WDS0,  0x4);	/* CPU:1, VPE:0 (PCIe0) */
		xps_queue_set(IFNAME_5G_WDS0,  0x4);	/* CPU:1, VPE:0 (PCIe0) */
		rps_queue_set(IFNAME_5G_WDS1,  0x4);	/* CPU:1, VPE:0 (PCIe0) */
		xps_queue_set(IFNAME_5G_WDS1,  0x4);	/* CPU:1, VPE:0 (PCIe0) */
		rps_queue_set(IFNAME_5G_WDS2,  0x4);	/* CPU:1, VPE:0 (PCIe0) */
		xps_queue_set(IFNAME_5G_WDS2,  0x4);	/* CPU:1, VPE:0 (PCIe0) */
		rps_queue_set(IFNAME_5G_WDS3,  0x4);	/* CPU:1, VPE:0 (PCIe0) */
		xps_queue_set(IFNAME_5G_WDS3,  0x4);	/* CPU:1, VPE:0 (PCIe0) */
#endif
		if (is_ap_mode) {
			rps_queue_set(IFNAME_MAC, 0x3);	/* CPU:0, VPE:0+1 */
			xps_queue_set(IFNAME_MAC, 0x3);	/* CPU:0, VPE:0+1 */
		} else {
			rps_queue_set(IFNAME_LAN, 0x2);	/* CPU:0, VPE:1 */
			xps_queue_set(IFNAME_LAN, 0x2);	/* CPU:0, VPE:1 */
			rps_queue_set(IFNAME_WAN, 0x2);	/* CPU:0, VPE:1 */
			xps_queue_set(IFNAME_WAN, 0x2);	/* CPU:0, VPE:1 */
#if defined (USE_SINGLE_MAC)
			rps_queue_set(IFNAME_MAC, 0x2);	/* CPU:0, VPE:1 */
			xps_queue_set(IFNAME_MAC, 0x2);	/* CPU:0, VPE:1 */
#endif
		}
		
	} else if (ncpu == 2) {
		irq_affinity_set(GIC_IRQ_FE,    1);	/* GMAC  -> CPU:0, VPE:0 */
		irq_affinity_set(GIC_IRQ_PCIE0, 2);	/* PCIe0 -> CPU:0, VPE:1 (usually rai0) */
		irq_affinity_set(GIC_IRQ_PCIE1, 1);	/* PCIe1 -> CPU:0, VPE:0 (usually ra0) */
		irq_affinity_set(GIC_IRQ_PCIE2, 1);	/* PCIe2 -> CPU:0, VPE:0 (usually ahci) */
		irq_affinity_set(GIC_IRQ_SDXC,  2);	/* SDXC  -> CPU:0, VPE:1 */
		irq_affinity_set(GIC_IRQ_XHCI,  2);	/* xHCI  -> CPU:0, VPE:1 */
		irq_affinity_set(GIC_IRQ_EIP93, 2);	/* EIP93 -> CPU:0, VPE:1 */
		
		rps_queue_set(IFNAME_2G_MAIN,  0x1);	/* CPU:0, VPE:0 (PCIe1) */
		xps_queue_set(IFNAME_2G_MAIN,  0x1);	/* CPU:0, VPE:0 (PCIe1) */
		rps_queue_set(IFNAME_2G_GUEST, 0x1);	/* CPU:0, VPE:0 (PCIe1) */
		xps_queue_set(IFNAME_2G_GUEST, 0x1);	/* CPU:0, VPE:0 (PCIe1) */
		rps_queue_set(IFNAME_2G_APCLI, 0x1);	/* CPU:0, VPE:0 (PCIe1) */
		xps_queue_set(IFNAME_2G_APCLI, 0x1);	/* CPU:0, VPE:0 (PCIe1) */
		rps_queue_set(IFNAME_2G_WDS0,  0x1);	/* CPU:0, VPE:0 (PCIe1) */
		xps_queue_set(IFNAME_2G_WDS0,  0x1);	/* CPU:0, VPE:0 (PCIe1) */
		rps_queue_set(IFNAME_2G_WDS1,  0x1);	/* CPU:0, VPE:0 (PCIe1) */
		xps_queue_set(IFNAME_2G_WDS1,  0x1);	/* CPU:0, VPE:0 (PCIe1) */
		rps_queue_set(IFNAME_2G_WDS2,  0x1);	/* CPU:0, VPE:0 (PCIe1) */
		xps_queue_set(IFNAME_2G_WDS2,  0x1);	/* CPU:0, VPE:0 (PCIe1) */
		rps_queue_set(IFNAME_2G_WDS3,  0x1);	/* CPU:0, VPE:0 (PCIe1) */
		xps_queue_set(IFNAME_2G_WDS3,  0x1);	/* CPU:0, VPE:0 (PCIe1) */
#if BOARD_HAS_5G_RADIO
		rps_queue_set(IFNAME_5G_MAIN,  0x2);	/* CPU:0, VPE:1 (PCIe0) */
		xps_queue_set(IFNAME_5G_MAIN,  0x2);	/* CPU:0, VPE:1 (PCIe0) */
		rps_queue_set(IFNAME_5G_GUEST, 0x2);	/* CPU:0, VPE:1 (PCIe0) */
		xps_queue_set(IFNAME_5G_GUEST, 0x2);	/* CPU:0, VPE:1 (PCIe0) */
		rps_queue_set(IFNAME_5G_APCLI, 0x2);	/* CPU:0, VPE:1 (PCIe0) */
		xps_queue_set(IFNAME_5G_APCLI, 0x2);	/* CPU:0, VPE:1 (PCIe0) */
		rps_queue_set(IFNAME_5G_WDS0,  0x2);	/* CPU:0, VPE:1 (PCIe0) */
		xps_queue_set(IFNAME_5G_WDS0,  0x2);	/* CPU:0, VPE:1 (PCIe0) */
		rps_queue_set(IFNAME_5G_WDS1,  0x2);	/* CPU:0, VPE:1 (PCIe0) */
		xps_queue_set(IFNAME_5G_WDS1,  0x2);	/* CPU:0, VPE:1 (PCIe0) */
		rps_queue_set(IFNAME_5G_WDS2,  0x2);	/* CPU:0, VPE:1 (PCIe0) */
		xps_queue_set(IFNAME_5G_WDS2,  0x2);	/* CPU:0, VPE:1 (PCIe0) */
		rps_queue_set(IFNAME_5G_WDS3,  0x2);	/* CPU:0, VPE:1 (PCIe0) */
		xps_queue_set(IFNAME_5G_WDS3,  0x2);	/* CPU:0, VPE:1 (PCIe0) */
#endif
		if (is_ap_mode) {
			rps_queue_set(IFNAME_MAC, 0x3);	/* CPU:0, VPE:0+1 */
			xps_queue_set(IFNAME_MAC, 0x3);	/* CPU:0, VPE:0+1 */
		} else {
			rps_queue_set(IFNAME_LAN, 0x1);	/* CPU:0, VPE:0 (GMAC) */
			xps_queue_set(IFNAME_LAN, 0x1);	/* CPU:0, VPE:0 (GMAC) */
			rps_queue_set(IFNAME_WAN, 0x1);	/* CPU:0, VPE:0 (GMAC) */
			xps_queue_set(IFNAME_WAN, 0x1);	/* CPU:0, VPE:0 (GMAC) */
#if defined (USE_SINGLE_MAC)
			rps_queue_set(IFNAME_MAC, 0x1);	/* CPU:0, VPE:0 (GMAC) */
			xps_queue_set(IFNAME_MAC, 0x1);	/* CPU:0, VPE:0 (GMAC) */
#endif
		}
	}
}

void
set_vpn_balancing(const char *vpn_ifname)
{
	/* set RPS/XPS balancing for PPP/SIT/TUN/TAP interfaces */
	int ncpu = sysconf(_SC_NPROCESSORS_ONLN);

	if (ncpu == 4) {
		rps_queue_set(vpn_ifname, 0x8);	/* CPU:1, VPE:1 */
		xps_queue_set(vpn_ifname, 0x8);	/* CPU:1, VPE:1 */
	} else if (ncpu == 2) {
		rps_queue_set(vpn_ifname, 0x2);	/* CPU:0, VPE:1 */
		xps_queue_set(vpn_ifname, 0x2);	/* CPU:0, VPE:1 */
	}
}
#else
inline void set_cpu_affinity(int is_ap_mode) {}
inline void set_vpn_balancing(const char *vpn_ifname) {}
#endif

#if defined (USE_NAND_FLASH)
void mount_rwfs_partition(void)
{
	int mtd_rwfs;
	char dev_ubi[16];
	const char *mp_rwfs = MP_MTD_RWFS;

	if (nvram_get_int("mtd_rwfs_mount") == 0)
		return;

	mtd_rwfs = mtd_dev_idx(MTD_PART_NAME_RWFS);
	if (mtd_rwfs < 3)
		return;

	module_smart_load("ubi", NULL);

	if (!is_module_loaded("ubi"))
		return;

	if (doSystem("ubiattach -p /dev/mtd%d -d %d", mtd_rwfs, mtd_rwfs) == 0) {
		mkdir(mp_rwfs, 0755);
		snprintf(dev_ubi, sizeof(dev_ubi), "/dev/ubi%d_0", mtd_rwfs);
		mount(dev_ubi, mp_rwfs, "ubifs", 0, NULL);
	}
}

void umount_rwfs_partition(void)
{
	int mtd_rwfs;
	const char *mp_rwfs = MP_MTD_RWFS;

	if (check_if_dir_exist(mp_rwfs)) {
		doSystem("/usr/bin/opt-umount.sh %s %s", "/dev/ubi", mp_rwfs);
		
		if (umount(mp_rwfs) == 0)
			rmdir(mp_rwfs);
	}

	mtd_rwfs = mtd_dev_idx(MTD_PART_NAME_RWFS);
	if (mtd_rwfs < 3)
		return;

	if (!is_module_loaded("ubi"))
		return;

	doSystem("ubidetach -p /dev/mtd%d 2>/dev/null", mtd_rwfs);
}

void start_rwfs_optware(void)
{
	int mtd_rwfs;
	char dev_ubi[16];
	const char *mp_rwfs = MP_MTD_RWFS;

	if (!check_if_dir_exist(mp_rwfs))
		return;

	mtd_rwfs = mtd_dev_idx(MTD_PART_NAME_RWFS);
	if (mtd_rwfs < 3)
		return;

	snprintf(dev_ubi, sizeof(dev_ubi), "/dev/ubi%d_0", mtd_rwfs);
	doSystem("/usr/bin/opt-mount.sh %s %s &", dev_ubi, mp_rwfs);
}
#else
inline void mount_rwfs_partition(void) {}
inline void umount_rwfs_partition(void) {}
inline void start_rwfs_optware(void) {}
#endif

void
kill_services(char* svc_name[], int wtimeout, int forcekill)
{
	int i, k, i_waited, i_killed;

	if (wtimeout < 1)
		wtimeout = 1;

	for (i=0;svc_name[i] && *svc_name[i];i++)
		doSystem("killall %s %s", "-q", svc_name[i]);

	for (k=0;k<wtimeout;k++) {
		i_waited = 0;
		for (i=0;svc_name[i] && *svc_name[i];i++) {
			if (pids(svc_name[i])) {
				i_waited = 1;
				break;
			}
		}
		
		if (!i_waited)
			break;
		
		sleep(1);
	}

	if (forcekill) {
		i_killed = 0;
		for (i=0;svc_name[i] && *svc_name[i];i++) {
			if (pids(svc_name[i])) {
				i_killed = 1;
				doSystem("killall %s %s", "-SIGKILL", svc_name[i]);
			}
		}
		if (i_killed)
			sleep(1);
	}
}

int
kill_process_pidfile(char *pidfile, int wtimeout, int forcekill)
{
	int i, result = -1;

	if (wtimeout < 1)
		wtimeout = 1;

	for (i=0; i<wtimeout; i++) {
		if (kill_pidfile(pidfile) != 0)
			break;
		result = 0; // process exist
		sleep(1);
	}

	if (i == wtimeout && forcekill) {
		if (kill_pidfile(pidfile) == 0)
			kill_pidfile_s(pidfile, SIGKILL);
	}

	return result;
}

int
create_file(const char *fn)
{
	int fd = open(fn, O_RDWR | O_CREAT, 0666);
	if (fd >= 0) {
		close(fd);
		return 0;
	}

	return 1;
}

int
check_if_dir_exist(const char *dirpath)
{
	struct stat stat_buf;

	if (!stat(dirpath, &stat_buf))
		return S_ISDIR(stat_buf.st_mode);
	else
		return 0;
}

int
check_if_file_exist(const char *filepath)
{
	struct stat stat_buf;

	if (!stat(filepath, &stat_buf))
		return S_ISREG(stat_buf.st_mode);
	else
		return 0;
}

int
check_if_dev_exist(const char *devpath)
{
	struct stat stat_buf;

	if (!stat(devpath, &stat_buf))
		return (S_ISCHR(stat_buf.st_mode) | S_ISBLK(stat_buf.st_mode));
	else
		return 0;
}

int
mkdir_if_none(const char *dirpath, const char *mode)
{
	DIR *dp;

	dp = opendir(dirpath);
	if (!dp)
		return !doSystem("mkdir -p -m %s %s", mode, dirpath);

	closedir(dp);
	return 0;
}

int
rename_if_dir_exist(const char *dir, const char *subdir)
{
	DIR *dirp;
	struct dirent *direntp;
	char oldpath[64], newpath[64];

	if (!dir || !subdir)
		return 0;

	if ((dirp = opendir(dir))) {
		while (dirp && (direntp = readdir(dirp))) {
			if (!strcasecmp(direntp->d_name, subdir) && strcmp(direntp->d_name, subdir)) {
				sprintf(oldpath, "%s/%s", dir, direntp->d_name);
				sprintf(newpath, "%s/%s", dir, subdir);
				rename(oldpath, newpath);
				return 1;
			}
		}

		closedir(dirp);
	}

	return 0;
}

char *
if_dircase_exist(const char *dir, const char *subdir)
{
	DIR *dirp;
	struct dirent *direntp;
	char oldpath[64];

	if (!dir || !subdir)
		return NULL;

	if ((dirp = opendir(dir))) {
		while (dirp && (direntp = readdir(dirp))) {
			if (!strcasecmp(direntp->d_name, subdir) && strcmp(direntp->d_name, subdir)) {
				sprintf(oldpath, "%s/%s", dir, direntp->d_name);
				return strdup(oldpath);
			}
		}
		closedir(dirp);
	}

	return NULL;
}

unsigned long
file_size(const char *filepath)
{
	struct stat stat_buf;

	if (!stat(filepath, &stat_buf) && S_ISREG(stat_buf.st_mode))
		return ((unsigned long) stat_buf.st_size);

	return 0;
}

// 1: add, 0: remove.
int
get_hotplug_action(const char *action)
{
	if (!strcmp(action, "remove"))
		return 0;

	return 1;
}