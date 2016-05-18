/*
 * Copyright (c) 2015 Parallels IP Holdings GmbH
 *
 * This file is part of OpenVZ. OpenVZ is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * Lesser General Public License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Our contact details: Parallels IP Holdings GmbH, Vordergasse 59, 8200
 * Schaffhausen, Switzerland.
 *
 * Common functions declarations
 */

#ifndef __VZCOMMON_H__
#define __VZCOMMON_H__

#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/queue.h>

#include <vzctl/libvzctl.h>

#include "vztt_types.h"

#define VZ_CONFDIR	"/etc/vz/"
#define VZ_CONFIG	VZ_CONFDIR "vz.conf"
#define PFCACHE_CONFIG	VZ_CONFDIR "pfcache.conf"
#define ENV_CONF_DIR	VZ_CONFDIR "conf/"
#define VE_CONFIG	"ve.conf"

#define VZTT_CONFDIR	"/etc/vztt/"
#define VZTT_CONFIG	VZTT_CONFDIR "vztt.conf"
#define VZTT_URL_MAP	VZTT_CONFDIR "url.map"
#define CACHE_VE_CONF   "vps.vzpkgtools.conf-sample"
#define JQUOTA_CONFIGURATION_PATH VZTT_CONFDIR "nojquota.conf"

#define TARGZ_SUFFIX		".tar.gz"
#define TARGZ_SUFFIX_LEN 	strlen(TARGZ_SUFFIX)

#define TARLZRW_SUFFIX		".tar.lzrw"
#define TARLZRW_SUFFIX_LEN	strlen(TARLZRW_SUFFIX)

#define TARLZ4_SUFFIX		".tar.lz4"
#define TARLZ4_SUFFIX_LEN	strlen(TARLZ4_SUFFIX)

#define PLOOP_SUFFIX		".ploop"
#define PLOOP_V2_SUFFIX		".ploopv2"
#define SIMFS_SUFFIX		".plain"

/* TODO: defined in vzctl/libvzctl.h */
#undef VZ_DIR
#define VZ_DIR		"/vz/"
#define TEMPLATE_DIR	VZ_DIR "template/"

#define PM_LIST_SUBDIR  "list/"
#define PM_DATA_DIR_NAME     "pm"
#define PM_DATA_SUBDIR  PM_DATA_DIR_NAME "/"

#define VERSION_LINK	"VERSION"
#define LAYOUT_LINK	".ve.layout"
#define MAXVERSIONLEN	100
#define VZCTLPATH	"/dev/vzctl"
#define VZCTL		"/usr/sbin/vzctl"
#define VZ_PKGENV_DIR	"/vz/pkgenv/"
#define VZ_TMP_DIR	"/vz/tmp/"
#define TAR		"tar"
#define PRL_COMPRESS	"prlcompress"
#define LZ4				"lz4"
#define YUM		"/usr/bin/yum"

#define LIBDIR 		"/usr/lib/"
#define LIB64DIR 	"/usr/lib64/"

#define BASEREPONAME	"base"
#define DEFSETNAME	"default"

#define ARCH_X86	"x86"
#define ARCH_X86_64	"x86_64"
#define ARCH_IA64	"ia64"
#define ARCH_I386	"i386"
#define ARCH_I586	"i586"
#define ARCH_AMD64	"amd64"
#define ARCH_NONE	"none"

//#define VZFS0_VERSION_LINK	"simfs"
#define VZFS0_VERSION_LINK	"005.000"
#define VZFS3_VERSION_LINK	"005.003"
#define VZFS4_VERSION_LINK	"005.004"

#define VZT_VE_LAYOUT4_LINK	"4"
#define VZT_VE_LAYOUT5_LINK	"5"
#define VZT_VE_LAYOUT3	VZCTL_LAYOUT_3
#define VZT_VE_LAYOUT4	VZCTL_LAYOUT_4
#define VZT_VE_LAYOUT5	VZCTL_LAYOUT_5	//support ploop

#define VZT_VE_LAYOUT5_ALIAS	"ploop"

#define VZT_ARCHIVE_GZ		1
#define VZT_ARCHIVE_LZRW	2
#define VZT_ARCHIVE_LZ4		3

#define VZT_CACHE_TYPE_VZFS	(1 << 0)
#define VZT_CACHE_TYPE_SIMFS	(1 << 1)
#define VZT_CACHE_TYPE_FSMASK	0x07

#define VZT_CACHE_TYPE_HOSTFS	(1 << 4)
#define VZT_CACHE_TYPE_PLOOP	(1 << 5)
#define VZT_CACHE_TYPE_PLOOP_V2	(1 << 6)
#define VZT_CACHE_TYPE_STMASK	0x70
#define VZT_CACHE_TYPE_ALL (VZT_CACHE_TYPE_SIMFS | \
	VZT_CACHE_TYPE_HOSTFS | VZT_CACHE_TYPE_PLOOP | VZT_CACHE_TYPE_PLOOP_V2)

#define VZCACHE 	1
#define VZCACHE2	2

#define METADATA_EXPIRE_DEF 86400	// one time at 24 hours
#define METADATA_EXPIRE_MAX 2147483647	// 0x7FFFFFFF

extern unsigned long available_technologies[];

/* size of buffer for file reading */
#define STRSIZ       4096

#define HTTP_PROXY	"http_proxy"
#define FTP_PROXY	"ftp_proxy"
#define HTTPS_PROXY	"https_proxy"

#ifdef __cplusplus
extern "C" {
#endif

/*
struct package
{
	char *name;
	char *arch;
	char *evr;
	char *descr;
	int marker;
};
*/
struct _url {
	char *proto;
	char *server;
	char *port;
	char *user;
	char *passwd;
	char *path;
};


#ifdef __cplusplus
};
#endif

#endif
