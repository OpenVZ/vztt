/*
 * Copyright (c) 2015-2017, Parallels International GmbH
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
 * Our contact details: Parallels International GmbH, Vordergasse 59, 8200
 * Schaffhausen, Switzerland.
 * 
 *  vztt structures
 */

#ifndef __VZTT_TYPES_H__
#define __VZTT_TYPES_H__

/* VZ template types */
#define VZ_TMPL_STD	0
#define VZ_TMPL_EZ	1

#ifdef __cplusplus
extern "C" {
#endif

/* vztt log file */
#define VZPKGLOG	"/var/log/vztt.log"
/* to suppress vztt stderr and stdout (for vzmdest) */
#define VZTT_QUIET	-127

/* package */
struct package
{
	char *name;
	char *arch;
	char *evr;
	char *descr;
	int marker;
};

/* package info */
struct pkg_info {
	char *name;
	char *version;
	char *release;
	char *arch;
	char *summary;
	char **description;
};

/* template info */
struct tmpl_info {
	char *name;
	char *osname;
	char *osver;
	char *osarch;
	char *confdir;
	char *summary;
	char **description;
	char **packages0;
	char **packages1;
	char **packages;
	char **repositories;
	char **mirrorlist;
	char *package_manager;
	char *package_manager_type;
	char *distribution;
	char **technologies;
	char **environment;
	char **upgradable_versions;
	char *cached;
};

/* group list */
struct group_list {
	char **installed;
	char **available;
};

/* group info */
struct group_info {
	char *name;
	char **list;
};

/* info fields mask : what to read */
#define VZTT_INFO_NONE			0
#define VZTT_INFO_NAME			(1U << 0)
#define VZTT_INFO_OSNAME		(1U << 1)
#define VZTT_INFO_VERSION		(1U << 2)
#define VZTT_INFO_RELEASE		(1U << 3)
#define VZTT_INFO_ARCH			(1U << 4)
#define VZTT_INFO_CONFDIR		(1U << 5)
#define VZTT_INFO_SUMMARY		(1U << 6)
#define VZTT_INFO_DESCRIPTION		(1U << 7)
#define VZTT_INFO_PACKAGES0		(1U << 8)
#define VZTT_INFO_PACKAGES1		(1U << 9)
#define VZTT_INFO_PACKAGES		(1U << 10)
#define VZTT_INFO_REPOSITORIES		(1U << 11)
#define VZTT_INFO_MIRRORLIST		(1U << 12)
#define VZTT_INFO_PACKAGE_MANAGER	(1U << 13)
#define VZTT_INFO_PACKAGE_MANAGER_TYPE	(1U << 14)
#define VZTT_INFO_DISTRIBUTION		(1U << 15)
#define VZTT_INFO_TECHNOLOGIES		(1U << 16)
#define VZTT_INFO_ENVIRONMENT		(1U << 17)
#define VZTT_INFO_UPGRADABLE_VERSIONS	(1U << 18)
#define VZTT_INFO_CACHED		(1U << 19)
/* This is an especial field. For save compatibility, if you use
 VZTT_INFO_OSRELEASE, VZTT_INFO_DISTRIBUTION will be overrided */
#define VZTT_INFO_OSRELEASE		(1U << 20)
#define VZTT_INFO_GOLDEN_IMAGE		(1U << 21)
/* This is an especial field. For save compatibility, if you use
 VZTT_INFO_JQUOTA, VZTT_INFO_DISTRIBUTION will be overrided */
#define VZTT_INFO_JQUOTA		(1U << 22)
#define VZTT_INFO_NO_PKG_ACTIONS	(1U << 23)

#define VZTT_INFO_TMPL_ALL	VZTT_INFO_NAME |\
				VZTT_INFO_OSNAME |\
				VZTT_INFO_VERSION |\
				VZTT_INFO_ARCH |\
				VZTT_INFO_CONFDIR |\
				VZTT_INFO_SUMMARY |\
				VZTT_INFO_DESCRIPTION |\
				VZTT_INFO_PACKAGES0 |\
				VZTT_INFO_PACKAGES1 |\
				VZTT_INFO_PACKAGES |\
				VZTT_INFO_REPOSITORIES |\
				VZTT_INFO_MIRRORLIST |\
				VZTT_INFO_PACKAGE_MANAGER |\
				VZTT_INFO_PACKAGE_MANAGER_TYPE |\
				VZTT_INFO_DISTRIBUTION |\
				VZTT_INFO_TECHNOLOGIES |\
				VZTT_INFO_ENVIRONMENT |\
				VZTT_INFO_UPGRADABLE_VERSIONS |\
				VZTT_INFO_CACHED |\
				VZTT_INFO_OSRELEASE |\
				VZTT_INFO_GOLDEN_IMAGE |\
				VZTT_INFO_NO_PKG_ACTIONS
#define VZTT_INFO_PKG_ALL	VZTT_INFO_NAME |\
				VZTT_INFO_VERSION |\
				VZTT_INFO_RELEASE |\
				VZTT_INFO_ARCH |\
				VZTT_INFO_SUMMARY |\
				VZTT_INFO_DESCRIPTION

/* template list element */
struct tmpl_list_el {
	char *timestamp;
	int is_os;
	struct tmpl_info *info;
};

/* values of opts->templates */
enum {
	OPT_TMPL_OS  = (1<<0),
	OPT_TMPL_APP = (1<<1),
};

/* values of opts->clean */
enum {
	OPT_CLEAN_PKGS  = (1<<0),
	OPT_CLEAN_TMPL = (1<<1),
};

/* values of opts->data_source */
enum {
	OPT_DATASOURCE_DEFAULT = 0,
	OPT_DATASOURCE_LOCAL = 1,
	OPT_DATASOURCE_REMOTE = 2,
};

/* values of opts->objects */
enum {
	OPT_OBJECTS_TEMPLATES = 0,
	OPT_OBJECTS_PACKAGES = 1,
	OPT_OBJECTS_GROUPS = 2,
};


struct options {
	char *logfile;
	int debug;
	int quiet;
	int force;
	int force_shared;
	int depends;
	int test;
	int data_source;
	int skiplock;
	int custom_pkg;
	int vz_dir;
	int objects;
	int templates;
	int clean;
	char *for_obj;
	int stdi_type;
	int stdi_version;
	int stdi_tech;
	int stdi_format;
	int only_std;
	int with_std;
	int cached_only;
	int pkgid;
	int force_openat;
	int expanded;
	unsigned long fld_mask;
	int interactive;
	int separate;
	int update_cache;
	int skip_db;
};

#ifdef __cplusplus
}
#endif

#endif
