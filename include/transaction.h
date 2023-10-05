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
 * Package manager declaration
 */

#include "vzcommon.h"
#include "config.h"
#include "options.h"

#ifndef _TRANSACTION_H_
#define _TRANSACTION_H_

#define DPKG "dpkg"
#define RPM "rpm"
#define RPM_ZYPP "rpmzypp"
#define PACKAGE_MANAGER_IS_RPM(package_manager) \
	strncmp(package_manager, RPM, strlen(RPM)) == 0
#define PACKAGE_MANAGER_IS_DPKG(package_manager) \
	strncmp(package_manager, DPKG, strlen(DPKG)) == 0
#define PACKAGE_MANAGER_IS_RPM_ZYPP(package_manager) \
	strncmp(package_manager, RPM_ZYPP, strlen(RPM_ZYPP)) == 0

#ifdef __cplusplus
extern "C" {
#endif

/* package manager actions */
typedef enum {
	VZPKG_UPGRADE,
	VZPKG_UPDATE,
	VZPKG_INSTALL,
	VZPKG_REMOVE,
	VZPKG_INFO,
	VZPKG_FETCH,
	VZPKG_AVAIL,
	VZPKG_LIST,
	VZPKG_CLEAN_METADATA,
	VZPKG_LOCALUPDATE,
	VZPKG_LOCALINSTALL,
	VZPKG_CLEAN,
	VZPKG_GET,
	VZPKG_MAKECACHE,
	VZPKG_GROUPINSTALL,
	VZPKG_GROUPUPDATE,
	VZPKG_GROUPREMOVE,
} pm_action_t;

#define STRUCT_TRANSACTION_CONTENT \
	ctid_t ctid;	\
	char *tmpdir;	\
	char *outfile;	\
	char *rootdir;	\
	char *envdir;	\
	char *pkgman;	\
	char *pkgarch;	\
	char *tmpldir;	\
	char *basedir;	\
	char *basesubdir;	\
	char *osrelease;	\
	struct _url http_proxy;	\
	struct _url ftp_proxy;	\
	struct _url https_proxy;\
	char *vzttproxy;	\
	/* template area subdirectory for metadata */\
	char *datadir;	\
	/* package manager type (RPM/DPKG) */\
	char *pm_type;	\
	struct tmpl_set *tdata;	\
	char *logfile;	\
	/* transaction flags */\
	int debug;	\
	int test;	\
	int force;	\
	int depends;	\
	int quiet;	\
	int data_source;	\
	int metadata_expire;	\
	int force_openat;	\
	int interactive;	\
	int expanded;	\
	int ign_pm_err; \
	unsigned long vzfs_technologies;	\
	struct string_list options;	\
	struct repo_list repositories;	\
	struct repo_list zypp_repositories;	\
	struct repo_list mirrorlists;	\
	struct url_map_list *url_map;	\
	/* exclude list */	\
	struct string_list exclude;	\
	/* pointers to functions of apt/yum wrappers */ \
	/* initialize */ \
	int (*pm_init)(struct Transaction *pm);	\
	/* cleanup */ \
	int (*pm_clean)(struct Transaction *pm);	\
	/* \
	 get installed into VE rpms list \
	 use external pm to work on stopped VE \
	 package manager root pass as extern parameter \
	 to use root (for runned) and private (for stopped VEs) areas. \
	*/ \
	int (*pm_get_install_pkg)(struct Transaction *pm, \
		struct package_list *packages);	\
	/* update metadata */ \
	int (*pm_update_metadata)(struct Transaction *pm, const char *name);\
	/* run pm transaction */ \
	int (*pm_action)( \
		struct Transaction *pm, \
		pm_action_t action, \
		struct string_list *packages);	\
	/* create root for rpm at <dir> */ \
	int (*pm_create_root)(char *dir);	\
	/* Find package directory in EZ template area */ \
	int (*pm_find_pkg_area)( \
		struct Transaction *pm, \
		struct package *pkg);	\
	/* find package directory in template area */ \
	int (*pm_find_pkg_area2)( \
		struct Transaction *pm, \
		struct package *pkg); \
	/* Find package directory in EZ template area */ \
	int (*pm_find_pkg_area_ex)( \
		struct Transaction *pm, \
		struct package *pkg, \
		char *dir, \
		size_t size);	\
	/* get internal package name */ \
	int (*pm_get_int_pkgname)(struct package *pkg, char *name, int size);	\
	/* get short package name */ \
	void (*pm_get_short_pkgname)(struct package *pkg, char *name, int size);	\
	/* fix package database (is actually for rom only) */ \
	int (*pm_fix_pkg_db)(struct Transaction *pm);	\
	/* check that pkgdir is package directory for standard template */ \
	int (*pm_is_std_pkg_area)(const char *pkgdir, struct package *pkg);	\
	/* compare two packages versions */ \
	int (*pm_ver_cmp)( \
		struct Transaction *pm, \
		const char * a, \
		const char * b, \
		int *eval);	\
	/* convert osarch to package arch */ \
	char *(*pm_os2pkgarch)(const char *osarch);	\
	/* find package in local cache  \
	 nva - directory name in template area (name_version_arch) */ \
	int (*pm_find_pkg_in_cache)( \
		struct Transaction *pm, \
		const char *nva, \
		char *path, \
		size_t size);	\
	/* compare conventional package name and name-arch \
	from package struct. evr ignored */ \
	int (*pm_pkg_cmp)(const char *pkg, struct package *p);	\
	/* remove packages */ \
	int (*pm_remove_pkg)( \
		struct Transaction *pm, \
		struct string_list *packages, \
		struct package_list *remains, \
		struct package_list *removed); \
	/* install/update local packages */ \
	int (*pm_run_local)( \
		struct Transaction *pm, \
		pm_action_t command, \
		struct string_list *packages,\
		struct package_list *added,\
		struct package_list *removed);\
	/* create OS template cache init - with /sbin/init */\
	int (*pm_create_init_cache)(\
		struct Transaction *pm,\
		struct string_list *packages0,\
		struct string_list *packages1,\
		struct string_list *packages,\
		struct package_list *installed);\
	/* create OS template cache - install post-init OS template packages */\
	int (*pm_create_post_init_cache)(\
		struct Transaction *pm,\
		struct string_list *packages0,\
		struct string_list *packages1,\
		struct string_list *packages,\
		struct package_list *installed);\
	/* create OS template cache - install OS template packages */\
	int (*pm_create_cache)(\
		struct Transaction *pm,\
		struct string_list *packages0,\
		struct string_list *packages1,\
		struct string_list *packages,\
		struct package_list *installed);\
	/* clean local cache */\
	int (*pm_clean_local_cache)(struct Transaction *pm);\
	/* get template package info, parse and place in structures */\
	int (*pm_tmpl_get_info)(\
		struct Transaction *pm,\
		const char *package,\
		struct pkg_info_list *ls);\
	/* get VE package info, parse and place in structures */\
	int (*pm_ve_get_info)(\
		struct Transaction *pm,\
		const char *package,\
		struct pkg_info_list *ls);\
	/* remove per-repositories template cache directories */\
	int (*pm_remove_local_caches)(struct Transaction *pm, char *reponame);\
	/* last repair chance: try fetch package <pkg> from <repair_mirror> */\
	int (*pm_last_repair_fetch)(\
		struct Transaction *pm,\
		struct package *pkg,\
		const char *repair_mirror);\
	int (*pm_vzttproxy_fetch)(\
		struct Transaction *pm,\
		struct package *pkg);\
	/* find (struct package *) in list<struct package *> \
	 for name, [epoch], version, release and arch (if arch is defined) \
	 Attn: pkg->evr should be _real_ package evr */\
	struct package_list_el * (*pm_package_find_nevra)(\
		struct package_list *packages,\
		struct package *pkg);\
	int (*pm_clean_metadata_symlinks)(\
		struct Transaction *pm,\
		char *name);\
	int (*pm_clone_metadata)(\
		struct Transaction *pm,\
		char *sname,\
		char *dname);\
	/* parse template area directory name and create struct package */\
	int (*pm_parse_vzdir_name)(char *dirname, struct package **pkg);\
	/* To get group list from yum, parse and place in structures */\
	int (*pm_get_group_list)(struct Transaction *pm, struct group_list *ls);\
	/* To get group info from yum, parse and place in structures */\
	int (*pm_get_group_info)(struct Transaction *pm, const char *group, struct group_info *group_info);\
	int progress_fd;\
	char *release_version;\
	int allow_erasing;

struct Transaction
{
STRUCT_TRANSACTION_CONTENT
};


int find_tmp_dir(char **tmp_dir);
int create_tmp_dir(char **tmp_dir);

/* create package manager object and initialize it */
int pm_init(
	const char *ctid,
	struct global_config *gc,
	struct vztt_config *tc,
	struct tmpl_set *tmpl,
	struct options_vztt *opts_vztt,
	struct Transaction **obj);

/* create package manager object and initialize it without vzup2date call */
int pm_init_wo_vzup2date(
	const char *ctid,
	struct global_config *gc,
	struct vztt_config *tc,
	struct tmpl_set *tmpl,
	struct options_vztt *opts_vztt,
	struct Transaction **obj);

/* cleanup */
int pm_clean(struct Transaction *pm);

/* create proxy environment variable and add into list <envs> */
int add_proxy_env(
		const struct _url *proxy,
		const  char *var,
		struct string_list *envs);
/* add templates environments to process environments list 
  Note: it is needs create env var VZCTL_ENV with full env var name list :
  VZCTL_ENV=:VAR_0:VAR_1:...:VAR_n
*/
int add_tmpl_envs(
	struct tmpl_set *tmpl,
	struct string_list *envs);

#ifndef CLONE_NEWUTS
#define CLONE_NEWUTS 0x04000000
#endif

struct clone_params {
	const char *cmd;
	const char *envdir;
	char **argv;
	char **envp;
	char *osrelease;
	int *fds;
	int ign_cmd_err;
	int debug;
	void *reader;
};



/* run <cmd> from chroot environment <envdir> with arguments <args> 
   and environments <envs>, 
   redirect <cmd> output to pipe and read by <reader> */
int run_from_chroot2(
		char *cmd,
		char *envdir,
		int debug,
		int ign_cmd_err,
		struct string_list *args,
		struct string_list *envs,
		char *osrelease,
		int reader(FILE *fp, void *data),
		void *data);
/* run <cmd> from chroot environment <envdir> with arguments <args> 
   and environments <envs> */
int run_from_chroot(
		char *cmd,
		char *envdir,
		int debug,
		int ign_cmd_err,
		struct string_list *args,
		struct string_list *envs,
		char *osrelease);

/* parse next string :
name [epoch:]version-release[ arch] description
and create structure
important: delimiter is _one_ space, string can not start from space
Debian case: sometime dpkg database (/var/lib/dpkg/status) have not
package architecture field at all, sometime for separate packages only

Used by yum & apt classes for rpm & dpkg output parsing
*/
int parse_p(char *str, struct package **pkg);

/* check undefined '$*' variable in url.
vars - list of internal variables for package manager */
int pm_check_url(char *url, char *vars[], int force);

/* set VZFS technologies set according veformat */
int pm_set_veformat(struct Transaction *pm, unsigned long veformat);
/* Is name official directory in os template directory dir */
int is_official_dir(const char *name);
/* run modify transaction for <packages> */
int pm_modify(struct Transaction *pm,
	pm_action_t action,
	struct string_list *packages,
	struct package_list *added,
	struct package_list *removed);
/* create file in tempoarry directory for apt/yum output */
int pm_create_outfile(struct Transaction *pm);
/* remove outfile */
int pm_remove_outfile(struct Transaction *pm);
/* get into VE vz packages list : read vzpackages file */
int pm_get_installed_vzpkg(
		struct Transaction *pm,
		char *ve_private, 
		struct package_list *packages);
/* get installed into VE vz packages list : 
   read vzpackages and compare with packages database
   This function will mount VE if needs */
int pm_get_installed_vzpkg2(
		struct Transaction *pm,
		const char *ctid,
		char *ve_private, 
		struct package_list *packages);
/* To get list of installed packages from VE rpm/deb db.
   Do not touch VE private, mount VE if it is stopped */
int pm_get_installed_pkg_from_ve(
		struct Transaction *pm,
		const char *ctid,
		struct package_list *installed);
/* find package with name <pname> in list <lst> */
int pm_find_in_list(
		struct Transaction *pm,
		struct package_list *lst,
		const char *pname);
/* is packages list up2date or not */
int pm_is_up2date(
		struct Transaction *pm,
		struct string_list *ls,
		struct package_list *installed,
		int *flag);
/* get available (in repos) list for installed packages */
int pm_get_available(
		struct Transaction *pm,
		struct package_list *installed,
		struct package_list *available);
/* save metadata file for template <name> */
int pm_save_metadata(struct Transaction *pm, const char *name);
/* create temporary root dir for package manager */
int pm_create_tmp_root(struct Transaction *pm);
/* set package manager root dir */
int pm_set_root_dir(struct Transaction *pm, const char *root_dir);
/* add exclude list */
int pm_add_exclude(struct Transaction *pm, const char *str);
/* fetch package and create directory in template ares */
int pm_prepare_pkg_area(struct Transaction *pm, struct package *pkg);
/* find package in local cache and remove 
 nva - directory name in template area */
int pm_rm_pkg_from_cache(struct Transaction *pm, const char *nva);
#ifdef __cplusplus
}
#endif

#endif
