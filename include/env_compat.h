/*
 * Copyright (c) 2015-2017, Parallels International GmbH
 * Copyright (c) 2017-2019 Virtuozzo International GmbH. All rights reserved.
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
 * Our contact details: Virtuozzo International GmbH, Vordergasse 59, 8200
 * Schaffhausen, Switzerland.
 *
 * Commom rpm functions for yum and zypper module declarations
 */

#ifndef _VZTT_ENV_COMPAT_H_
#define _VZTT_ENV_COMPAT_H_

#include "vzcommon.h"
#include "transaction.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NAME_TITLE "Name:"
#define ARCH_TITLE "Arch:"
#define VERSION_TITLE "Version:"
#define RELEASE_TITLE "Release:"
#define SUMMARY_TITLE "Summary:"
#define DESC_TITLE "Description:"
#define NAME_TITLE_RPM		"Name   :"
#define NAME_TITLE_RPM_LONG "Name     \t   :"
#define ARCH_TITLE_RPM "Arch   :"


int read_rpm_info(FILE *fp, void *data);

int fetch_mirrorlist(
		struct Transaction *pm,
		char *mirrorlist,
		char *buf,
		int size);

int env_compat_get_install_pkg(
		struct Transaction *pm,
		struct package_list *packages);


int env_compat_fix_pkg_db(struct Transaction *pm);

int env_compat_get_int_pkgname(
		struct package *pkg,
		char *name,
		int size);

void env_compat_get_short_pkgname(
		struct package *pkg,
		char *name,
		int size);

int env_compat_is_std_pkg_area(
		const char *pkgdir,
		struct package *pkg);

int env_compat_create_root(char *dir);

int env_compat_find_pkg_area(
		struct Transaction *pm,
		struct package *pkg);

int env_compat_find_pkg_area2(
		struct Transaction *pm,
		struct package *pkg);

int env_compat_find_pkg_area_ex(
		struct Transaction *pm,
		struct package *pkg,
		char *dir,
		size_t size);

int env_compat_ver_cmp(
		struct Transaction *pm,
		const char * a,
		const char * b,
		int *eval);

int env_compat_update_metadata(
		struct Transaction *pm,
		const char *name);

int env_compat_pkg_cmp(
		const char *pkg,
		struct package *p);

int env_compat_remove_rpm(
		struct Transaction *pm,
		struct string_list *packages,
		struct package_list *remains,
		struct package_list *removed);

int env_compat_create_cache(
		struct Transaction *pm,
		struct string_list *packages0,
		struct string_list *packages1,
		struct string_list *packages,
		struct package_list *installed);

int env_compat_clean_local_cache(struct Transaction *pm);

int env_compat_rpm_get_info(
		struct Transaction *pm,
		const char *package,
		struct pkg_info_list *ls);

int env_compat_parse_repo_rec(
		const char *rec,
		struct url_map_list *url_map,
		struct string_list *ls,
		int force);

int env_compat_vzttproxy_fetch(
		struct Transaction *pm,
		struct package *pkg);

int env_compat_last_repair_fetch(
		struct Transaction *pm,
		struct package *pkg,
		const char *repair_mirror);

int env_compat_parse_vzdir_name(
		char *dirname,
		struct package **pkg);

struct package_list_el * env_compat_package_find_nevra(
		struct package_list *packages,
		struct package *pkg);

int env_compat_create_init_cache(
		struct Transaction *pm,
		struct string_list *packages0,
		struct string_list *packages1,
		struct string_list *packages,
		struct package_list *installed);

int env_compat_create_post_init_cache(
		struct Transaction *pm,
		struct string_list *packages0,
		struct string_list *packages1,
		struct string_list *packages,
		struct package_list *installed);

int env_compat_get_group_list(
		struct Transaction *pm,
		struct group_list *ls);

int env_compat_get_group_info(
		struct Transaction *pm,
		const char *group,
		struct group_info *group_info);

int env_compat_run_local(
		struct Transaction *pm,
		pm_action_t cmd,
		struct string_list *packages,
		struct package_list *added,
		struct package_list *removed);

#ifdef __cplusplus
}
#endif
#endif
