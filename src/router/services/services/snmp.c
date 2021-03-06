/*
 * snmp.c
 *
 * Copyright (C) 2006 Sebastian Gottschall <gottschall@dd-wrt.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * $Id:
 */

#ifdef HAVE_SNMP

#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <bcmnvram.h>
#include <shutils.h>
#include <utils.h>
#include <syslog.h>
#include <services.h>
#include "snmp.h"

#define SNMP_CONF_FILE	"/var/snmp/snmpd.conf"

void start_snmp(void)
{
	pid_t pid;

	char *snmpd_argv[] = { "snmpd", "-c", SNMP_CONF_FILE, NULL };
	FILE *fp = NULL;

	stop_snmp();

	if (!nvram_invmatchi("snmpd_enable", 0))
		return;
#ifdef HAVE_NEXTMEDIA
	if (f_exists("/jffs/etc/snmpd.conf")) {
		sysprintf("ln -s /jffs/etc/snmpd.conf " SNMP_CONF_FILE);
	} else {
#endif

		fp = fopen(SNMP_CONF_FILE, "w");
		if (NULL == fp)
			return;

		if (strlen(nvram_safe_get("snmpd_syslocation")) > 0)
			fprintf(fp, "syslocation %s\n", nvram_safe_get("snmpd_syslocation"));
		if (strlen(nvram_safe_get("snmpd_syscontact")) > 0)
			fprintf(fp, "syscontact %s\n", nvram_safe_get("snmpd_syscontact"));
		if (strlen(nvram_safe_get("snmpd_sysname")) > 0)
			fprintf(fp, "sysname %s\n", nvram_safe_get("snmpd_sysname"));
		if (strlen(nvram_safe_get("snmpd_rocommunity")) > 0)
			fprintf(fp, "rocommunity %s\n", nvram_safe_get("snmpd_rocommunity"));
		if (strlen(nvram_safe_get("snmpd_rwcommunity")) > 0)
			fprintf(fp, "rwcommunity %s\n", nvram_safe_get("snmpd_rwcommunity"));
		fprintf(fp, "sysservices 9\n");
#ifdef HAVE_NEXTMEDIA
		if (!f_exists("/jffs/custom_snmp/snmpd.tail")) {
#endif
#ifdef HAVE_RAYTRONIK
			fprintf(fp, "pass_persist .1.3.6.1.4.1.41404.255 /etc/config/wl_snmpd.sh\n");
#else
			fprintf(fp, "pass_persist .1.3.6.1.4.1.2021.255 /etc/wl_snmpd.sh\n");
#endif
			fclose(fp);
#ifdef HAVE_NEXTMEDIA
		} else {
			fclose(fp);
			sysprintf("cat /jffs/custom_snmp/snmpd.tail >> " SNMP_CONF_FILE);
		}
	}
#endif
	_evalpid(snmpd_argv, NULL, 0, &pid);

	cprintf("done\n");
	dd_syslog(LOG_INFO, "snmpd : SNMP daemon successfully started\n");

	return;
}

void stop_snmp(void)
{
	stop_process("snmpd", "SNMP Daemon");
	return;
}
#endif
