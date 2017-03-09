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
 *  vztt function definitions
 */

#ifndef __VZTT_H__
#define __VZTT_H__

#include "vztt_error.h"
#include "vztt_types.h"
#include "vztt_options.h"

#ifndef _USE_DLOPEN_

#ifdef __cplusplus
extern "C" {
#endif

#define PROGRESS_STAGE_PREFIX "stage="
#define PROGRESS_PERCENT_PREFIX "percent="
#define PROGRESS_DELIMITER " "
#define PROGRESS_END "\n"

/*
 get list of `custom` packages:
 packages, installed in VE but not available in repositoies
*/
int vztt_get_custom_pkgs(
		const char *ctid,
		struct options *opts,
		struct package ***packages);

/* get list of installed into VE <veid> packages as char ***<packages> */
int vztt_get_ve_pkgs(
		const char *ctid,
		struct options *opts,
		struct package ***packages);

/* get list of packages directories it template area, used for VE <veid> */
int vztt_get_vzdir(const char *ctid, struct options *opts, char ***vzdir);

/* get list of available packages for template <tname> */
int vztt_get_template_pkgs(
		char *tname,
		struct options *opts,
		struct package ***packages);

/* get VE status - up2date or not */
extern int vztt_get_ve_status(
	const char *ctid,
	struct options *opts);

/* get OS template cache status - up2date or not */
extern int vztt_get_cache_status(
	char *ostemplate,
	struct options *opts);

/*
 Repair template area:
 download installed in VE private area packages
 and unpack content in template area
*/
extern int vztt_repair(
	const char *ostemplate,
	const char *ve_private,
	struct options *opts);

/* update OS and apps templates metadata */
extern int vztt_update_metadata(
	char *ostemplate,
	struct options *opts);

/* template area cleanup: remove unused packages directories */
extern int vztt_cleanup(
	char *ostemplate,
	struct options *opts);

/* upgrade VE to new OS template */
extern int vztt_upgrade(
	char *arg,
	struct options *opts,
	struct package ***pkg_updated,
	struct package ***pkg_added,
	struct package ***pkg_removed,
	struct package ***pkg_converted);

/* install packages into VE */
extern int vztt_install(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options *opts,
	struct package ***pkg_added,
	struct package ***pkg_removed);

/* update packages in VE */
extern int vztt_update(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options *opts,
	struct package ***pkg_added,
	struct package ***pkg_removed);

/* remove packages from VE */
extern int vztt_remove(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options *opts,
	struct package ***pkg_removed);

/* install templates into VE */
extern int vztt_install_tmpl(
	const char *ctid,
	char *tlist[],
	size_t size,
	struct options *opts,
	struct package ***pkg_updated,
	struct package ***pkg_removed);

/* update templates in VE */
extern int vztt_update_tmpl(
	const char *ctid,
	char *tlist[],
	size_t size,
	struct options *opts,
	struct package ***pkg_added,
	struct package ***pkg_removed);

/* remove templates from VE */
extern int vztt_remove_tmpl(
	const char *ctid,
	char *tlist[],
	size_t size,
	struct options *opts,
	struct package ***pkg_removed);

/* install local packages */
int vztt_localinstall(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options *opts,
	struct package ***pkg_added,
	struct package ***pkg_removed);

/* update local packages */
int vztt_localupdate(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options *opts,
	struct package ***pkg_added,
	struct package ***pkg_removed);

/* create cache for os template */
int vztt_create_cache(
	char *ostemplate,
	struct options *opts,
	int skip_existed);

/* update cache for os template */
int vztt_update_cache(
	char *ostemplate,
	struct options *opts);

/* remove cache file */
int vztt_remove_cache(
	char *ostemplate,
	struct options *opts);

/* Link VE: change regular file to magic in VE private area */
int vztt_link(
	const char *ctid,
	struct options *opts);

/* Fetch packages for all os and app templates */
int vztt_fetch(
	char *ostemplate,
	struct options *opts);
/* Fetch packages for os and app templates separately, per-template */
int vztt_fetch_separately(
	char *ostemplate,
	struct options *opts);

/* get package info */
int vztt_get_pkg_info(
	char *ostemplate,
	char *package,
	struct options *opts,
	struct pkg_info ***arr);

/* clean package info array */
void vztt_clean_pkg_info(struct pkg_info ***arr);

/* Get all installed base OS template */
int vztt_get_all_base(char ***arr);

/* get OS template info */
int vztt_get_os_tmpl_info(
	char *ostemplate,
	struct options *opts,
	struct tmpl_info *info);

/* get application template info */
int vztt_get_app_tmpl_info(
	char *ostemplate,
	char *app,
	struct options *opts,
	struct tmpl_info *info);

/* clean template info */
void vztt_clean_tmpl_info(struct tmpl_info *info);

/* get templates list for <ostemplate> */
int vztt_get_templates_list(
		char *ostemplate,
		struct options *opts,
		struct tmpl_list_el ***ls);

/* get templates list for <veid> */
int vztt_get_ve_templates_list(
		const char *ctid,
		struct options *opts,
		struct tmpl_list_el ***ls);
/* clean template list */
void vztt_clean_templates_list(struct tmpl_list_el **ls);

/* install template as rpm package <rpm> on HN */
int vztt_install_template(
		char **rpms,
		size_t sz,
		struct options *opts,
		char ***arr);
/* update template as rpm package <rpm> on HN */
int vztt_update_template(
		char **rpms,
		size_t sz,
		struct options *opts,
		char ***arr);
/* remove os template <tmpl> from HN */
int vztt_remove_os_template(char *tmpl, struct options *opts);
/* remove app template <tmpl> from HN */
int vztt_remove_app_template(char *tmpl, struct options *opts);

/* return 1 if <name> is standard template name */
int vztt_is_std_template(char *name);
/* get ve os template */
int vztt_get_ve_ostemplate(const char *ctid, char **ostemplate, int *tmpl_type);

/* lock ostemplate */
int vztt_lock_ostemplate(const char *ostemplate, void **lockdata);
/* unlock ostemplate */
void vztt_unlock_ostemplate(void *lockdata);

/* logfile & loglevel initialization */
void vztt_init_logger(const char * logfile, int loglevel);

/* 
 Upgrade template area from vzfs3 to vzfs4
*/
int vztt_upgrade_area(char *ostemplate, struct options *opts);
/* check vzfs version of directories in template area,
used for VE <veid> */
int vztt_check_vzdir(const char *ctid, struct options *opts);
/* for backup: get list of installed apptemplates via vzpackages list from <veprivate>
   and template variables from <veconfig> */
int vztt_get_backup_apps(
		char *veprivate,
		char *veconfig,
		struct options *opts,
		char ***arr);
/* set default vztt options */
void vztt_set_default_options(struct options *opts);
/* does <rpm> provides ez template */
int vztt_is_ez_rpm(const char *rpm);

/* clean package list */
void vztt_clean_packages_list(struct package **ls);

/*
 To read VE package manager database and correct file vzpackages & app templates list
*/
int vztt_sync_vzpackages(const char *ctid, struct options *opts);

/* run <cmd> with <argv> from chroot() for <ostemplate> */
int vztt_run_from_chroot(
	const char *ostemplate,
	const char *cmd,
	const char *argv[],
	struct options *opts);

/* install groups of packages into VE (for rpm-based templates only) */
int vztt_install_group(
	const char *ctid,
	char *groups[],
	size_t size,
	struct options *opts,
	struct package ***pkg_added,
	struct package ***pkg_removed);

/* update groups of packages in VE (for rpm-based templates only) */
int vztt_update_group(
	const char *ctid,
	char *groups[],
	size_t size,
	struct options *opts,
	struct package ***pkg_updated,
	struct package ***pkg_removed);

/* remove groups of packages from VE (for rpm-based templates only) */
int vztt_remove_group(
	const char *ctid,
	char *groups[],
	size_t size,
	struct options *opts,
	struct package ***pkg_removed);

/* get list of installed into VE <veid> groups of packages */
int vztt_get_ve_groups(const char *ctid, struct options *opts, struct group_list *groups);

/* get list of available groups of packages for template <tname> (for rpm-based templates only) */
int vztt_get_template_groups(char *tname, struct options *opts, struct group_list *groups);

/* clean group list */
void vztt_clean_group_list(struct group_list *groups);

/* get group of packages info */
int vztt_get_group_info(const char *arg, const char *group, struct options *opts, struct group_info *info);

/* clean group info */
void vztt_clean_group_info(struct group_info *info);

/* vztt2 functions declaration */
/* install templates into VE */
extern int vztt2_install_tmpl(
	const char *ctid,
	char *tlist[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_updated,
	struct package ***pkg_removed);

/* update templates in VE */
extern int vztt2_update_tmpl(
	const char *ctid,
	char *tlist[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_added,
	struct package ***pkg_removed);

/* remove templates from VE */
extern int vztt2_remove_tmpl(
	const char *ctid,
	char *tlist[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_removed);

/* create cache for os template */
int vztt2_create_cache(
	char *ostemplate,
	struct options_vztt *opts_vztt,
	int skip_existed);

/* update cache for os template */
int vztt2_update_cache(
	char *ostemplate,
	struct options_vztt *opts_vztt);

/* remove cache file */
int vztt2_remove_cache(
	char *ostemplate,
	struct options_vztt *opts_vztt);

/* install packages into VE */
extern int vztt2_install(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_added,
	struct package ***pkg_removed);

/* update packages in VE */
extern int vztt2_update(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_added,
	struct package ***pkg_removed);

/* remove packages from VE */
extern int vztt2_remove(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_removed);

/* install groups of packages into VE (for rpm-based templates only) */
int vztt2_install_group(
	const char *ctid,
	char *groups[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_added,
	struct package ***pkg_removed);

/* update groups of packages in VE (for rpm-based templates only) */
int vztt2_update_group(
	const char *ctid,
	char *groups[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_updated,
	struct package ***pkg_removed);

/* remove groups of packages from VE (for rpm-based templates only) */
int vztt2_remove_group(
	const char *ctid,
	char *groups[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_removed);

/* get list of installed into VE <veid> groups of packages */
int vztt2_get_ve_groups(
	const char *ctid,
	struct options_vztt *opts_vztt,
	struct group_list *groups);

/* get list of available groups of packages for template <tname> (for rpm-based templates only) */
int vztt2_get_template_groups(
	char *tname,
	struct options_vztt *opts_vztt,
	struct group_list *groups);

/* get group of packages info */
int vztt2_get_group_info(
	const char *arg,
	const char *group,
	struct options_vztt *opts_vztt,
	struct group_info *info);

/* install local packages */
int vztt2_localinstall(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_added,
	struct package ***pkg_removed);

/* update local packages */
int vztt2_localupdate(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_added,
	struct package ***pkg_removed);

int vztt2_get_custom_pkgs(
		const char *ctid,
		struct options_vztt *opts_vztt,
		struct package ***packages);

/* get list of installed into VE <veid> packages as char ***<packages> */
int vztt2_get_ve_pkgs(
		const char *ctid,
		struct options_vztt *opts_vztt,
		struct package ***packages);

/* get list of installed into VE <veid> groups of packages */
int vztt2_get_ve_groups(
		const char *ctid,
		struct options_vztt *opts_vztt,
		struct group_list *groups);

/* get list of packages directories it template area, used for VE <veid> */
int vztt2_get_vzdir(const char *ctid, struct options_vztt *opts_vztt, char ***vzdir);

/* get list of available packages for template <tname> */
int vztt2_get_template_pkgs(
		char *tname,
		struct options_vztt *opts_vztt,
		struct package ***packages);

/* get list of available groups of packages for template <tname> (for rpm-based templates only) */
int vztt2_get_template_groups(
		char *tname,
		struct options_vztt *opts_vztt,
		struct group_list *groups);

/* get templates list for <ostemplate> */
int vztt2_get_templates_list(
		char *ostemplate,
		struct options_vztt *opts_vztt,
		struct tmpl_list_el ***ls);

/* get templates list for <veid> */
int vztt2_get_ve_templates_list(
		const char *ctid,
		struct options_vztt *opts_vztt,
		struct tmpl_list_el ***ls);

/* for backup: get list of installed apptemplates via vzpackages list from <veprivate>
   and template variables from <veconfig> */
int vztt2_get_backup_apps(
		char *veprivate,
		char *veconfig,
		struct options_vztt *opts_vztt,
		char ***arr);

/* get VE status - up2date or not */
int vztt2_get_ve_status(
	const char *ctid,
	struct options_vztt *opts_vztt);

/* get OS template cache status - up2date or not */
extern int vztt2_get_cache_status(
	char *ostemplate,
	struct options_vztt *opts_vztt);

/*
 Repair template area:
 download installed in VE private area packages
 and unpack content in template area
*/
extern int vztt2_repair(
	const char *ostemplate,
	const char *ve_private,
	struct options_vztt *opts_vztt);

/* Link VE: change regular file to magic in VE private area */
int vztt2_link(
	const char *ctid,
	struct options_vztt *opts_vztt);

/*
 To read VE package manager database and correct file vzpackages
 & app templates list
*/
int vztt2_sync_vzpackages(const char *ctid, struct options_vztt *opts_vztt);

/* Fetch packages for all os and app templates */
int vztt2_fetch(
	char *ostemplate,
	struct options_vztt *opts_vztt);

/* Fetch packages for os and app templates separately, per-template */
int vztt2_fetch_separately(
	char *ostemplate,
	struct options_vztt *opts_vztt);

/* install template as rpm package <rpm> on HN */
int vztt2_install_template(
		char **rpms,
		size_t sz,
		struct options_vztt *opts_vztt,
		char ***arr);

/* update template as rpm package <rpm> on HN */
int vztt2_update_template(
		char **rpms,
		size_t sz,
		struct options_vztt *opts_vztt,
		char ***arr);

/* remove os template <tmpl> from HN */
int vztt2_remove_os_template(char *tmpl, struct options_vztt *opts_vztt);

/* remove app template <tmpl> from HN */
int vztt2_remove_app_template(char *tmpl, struct options_vztt *opts_vztt);

/*
 Upgrade template area from vzfs3 to vzfs4
*/
int vztt2_upgrade_area(char *ostemplate, struct options_vztt *opts_vztt);

/* run <cmd> with <argv> from chroot() for <ostemplate> */
int vztt2_run_from_chroot(
	const char *ostemplate,
	const char *cmd,
	const char *argv[],
	struct options_vztt *opts_vztt);

/* upgrade VE to new OS template */
extern int vztt2_upgrade(
	char *arg,
	struct options_vztt *opts_vztt,
	struct package ***pkg_updated,
	struct package ***pkg_added,
	struct package ***pkg_removed,
	struct package ***pkg_converted);

/* template area cleanup: remove unused packages directories */
extern int vztt2_cleanup(
	char *ostemplate,
	struct options_vztt *opts_vztt);

/* get package info */
int vztt2_get_pkg_info(
	char *ostemplate,
	char *package,
	struct options_vztt *opts_vztt,
	struct pkg_info ***arr);

/* get application template info */
int vztt2_get_app_tmpl_info(
	char *ostemplate,
	char *app,
	struct options_vztt *opts_vztt,
	struct tmpl_info *info);

/* get OS template info */
int vztt2_get_os_tmpl_info(
	char *ostemplate,
	struct options_vztt *opts_vztt,
	struct tmpl_info *info);

/* get group of packages info */
int vztt2_get_group_info(
	const char *arg,
	const char *group,
	struct options_vztt *opts_vztt,
	struct group_info *info);

/* update OS and apps templates metadata */
extern int vztt2_update_metadata(
	char *ostemplate,
	struct options_vztt *opts_vztt);

/* check vzfs version of directories in template area,
used for VE <veid> */
int vztt2_check_vzdir(const char *ctid, struct options_vztt *opts_vztt);

/* create os template cache with applications */
int vztt2_create_appcache(struct options_vztt *opts_vztt, int recreate);

/* update os template cache with applications */
int vztt2_update_appcache(struct options_vztt *opts_vztt);

/* remove os template cache with applications */
int vztt2_remove_appcache(struct options_vztt *opts_vztt);

/* list os template cache with applications */
int vztt2_list_appcache(struct options_vztt *opts_vztt);

/* get list of packages directories it template area, used for template cache */
int vztt2_get_cache_vzdir(
		const char *ostemplate,
		struct options_vztt *opts_vztt,
		char *cache_path,
		size_t size,
		char ***vzdir);

/* get list of all packages directories in template area */
int vztt2_get_all_pkgs_vzdir(
		const char *ostemplate,
		struct options_vztt *opts_vztt,
		char ***vzdir);

#ifdef __cplusplus
}
#endif

#endif /* _USE_DLOPEN_ */

#endif /* __VZTT_H__ */
