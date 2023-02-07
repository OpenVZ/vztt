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
 * Yum wrapper
 */

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <dirent.h>
#include <error.h>
#include <limits.h>
#include <signal.h>
#include <getopt.h>
#include <libgen.h>
#include <ctype.h>

#include "vzcommon.h"
#include "vztt_error.h"
#include "tmplset.h"
#include "transaction.h"
#include "env_compat.h"
#include "yum.h"
#include "downloader.h"
#include "util.h"
#include "vztt.h"
#include "progress_messages.h"


int yum_init(struct Transaction *pm);
int yum_clean(struct Transaction *pm);
int yum_action(
		struct Transaction *pm,
		pm_action_t action,
		struct string_list *packages);
char *yum_os2pkgarch(const char *osarch);
int yum_find_pkg_in_cache(
		struct Transaction *pm,
		const char *dname,
		char *path,
		size_t size);
int yum_get_info(
		struct Transaction *pm, 
		const char *package, 
		struct pkg_info_list *ls);
int yum_remove_local_caches(struct Transaction *pm, char *reponame);
int yum_clone_metadata(
		struct Transaction *pm, 
		char *sname, 
		char *dname);
int yum_clean_metadata_symlinks(
		struct Transaction *pm, 
		char *name);
int yum_get_group_list(struct Transaction *pm, struct group_list *ls);
int yum_get_group_info(struct Transaction *pm, const char *group, struct group_info *group_info);

static int yum_move_local_caches(struct YumTransaction *yum);

/* set yum command line default arguments */
static int yum_set_default_arguments(struct YumTransaction *yum, struct string_list *args);

/* create structure */
int yum_create(struct Transaction **pm)
{
	*pm = (struct Transaction *)malloc(sizeof(struct YumTransaction));
	if (*pm == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory\n");
		return VZT_CANT_ALLOC_MEM;
	}
	memset((void *)(*pm), 0, sizeof(struct YumTransaction));
	/* set wrapper functions */
	(*pm)->pm_init = yum_init;
	(*pm)->pm_clean = yum_clean;
	(*pm)->pm_get_install_pkg = env_compat_get_install_pkg;
	(*pm)->pm_update_metadata = env_compat_update_metadata;
	(*pm)->pm_action = yum_action;
	(*pm)->pm_create_root = env_compat_create_root;
	(*pm)->pm_find_pkg_area = env_compat_find_pkg_area;
	(*pm)->pm_find_pkg_area2 = env_compat_find_pkg_area2;
	(*pm)->pm_find_pkg_area_ex = env_compat_find_pkg_area_ex;
	(*pm)->pm_get_int_pkgname = env_compat_get_int_pkgname;
	(*pm)->pm_get_short_pkgname = env_compat_get_short_pkgname;
	(*pm)->pm_fix_pkg_db = env_compat_fix_pkg_db;
	(*pm)->pm_is_std_pkg_area = env_compat_is_std_pkg_area;
	(*pm)->pm_ver_cmp = env_compat_ver_cmp;
	(*pm)->pm_os2pkgarch = yum_os2pkgarch;
	(*pm)->pm_find_pkg_in_cache = yum_find_pkg_in_cache;
	(*pm)->pm_pkg_cmp = env_compat_pkg_cmp;
	(*pm)->pm_remove_pkg = env_compat_remove_rpm;
	(*pm)->pm_run_local = env_compat_run_local;
	(*pm)->pm_create_init_cache = env_compat_create_init_cache;
	(*pm)->pm_create_post_init_cache = env_compat_create_post_init_cache;
	(*pm)->pm_create_cache = env_compat_create_cache;
	(*pm)->pm_clean_local_cache = env_compat_clean_local_cache;
	(*pm)->pm_ve_get_info = env_compat_rpm_get_info;
	(*pm)->pm_tmpl_get_info = yum_get_info;
	(*pm)->pm_remove_local_caches = yum_remove_local_caches;
	(*pm)->pm_last_repair_fetch = env_compat_last_repair_fetch;
	(*pm)->pm_vzttproxy_fetch = env_compat_vzttproxy_fetch;
	(*pm)->pm_package_find_nevra = env_compat_package_find_nevra;
	(*pm)->pm_clean_metadata_symlinks = yum_clean_metadata_symlinks;
	(*pm)->pm_clone_metadata = yum_clone_metadata;
	(*pm)->pm_parse_vzdir_name = env_compat_parse_vzdir_name;
	(*pm)->pm_get_group_list = yum_get_group_list;
	(*pm)->pm_get_group_info = yum_get_group_info;
	(*pm)->datadir = PM_DATA_SUBDIR;
	(*pm)->pm_type = RPM;

	return 0;
}


/* initialize */
int yum_init(struct Transaction *pm)
{
	char path[PATH_MAX+1];
	char *libdir;
	const char *pp = "PYTHONPATH=";
	int i, p;
	char *ptr;
	struct stat st;
	int lfound = 0;
	struct YumTransaction *yum = (struct YumTransaction *)pm;

	yum->yum_conf = NULL;

	// specify libdir
	if (strcmp(yum->pkgarch, ARCH_X86_64) == 0)
		libdir = LIB64DIR;
	else
		libdir = LIBDIR;

	/* get python version for yum */
	/* try to find usr/lib[64]*\/python(2|3).* directory in environment */
	for (p = 2; p <= 4; p++) {
		for (i = 2; i <= 20; i++) {
			snprintf(path, sizeof(path), "%s%s%s/python%d.%d", pp, \
				pm->envdir, libdir, p, i);
			ptr = path + strlen(pp);
			if (stat(ptr, &st) == 0) {
				if (S_ISDIR(st.st_mode)) {
					lfound = 1;
					break;
				}
			}
		}
		if (lfound)
			break;
	}
	if (!lfound) {
		vztt_logger(0, 0, "Python directory not found in %s", pm->envdir);
		return VZT_ENVDIR_BROKEN;
	}
	if ((yum->pythonpath = strdup(path)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	/* move /vz/template/<osname>/<osver>/<osarch>/<repoid>* directories to
	   /vz/template/<osname>/<osver>/<osarch>/yum/ on first start only */
	yum_move_local_caches(yum);

	return 0;
}

/* cleanup */
int yum_clean(struct Transaction *pm)
{
	struct YumTransaction *yum = (struct YumTransaction *)pm;

	VZTT_FREE_STR(yum->pythonpath);
	/* TODO: should we call yum_remove_config here? */
	VZTT_FREE_STR(yum->yum_conf);
	
	return 0;
}

/* rewrote fetched mirrorlist file: dublicate all records for vzttproxy */
static int mirrorlist_add_vzttproxy(const char *path, const char *vzttproxy)
{
	FILE *fd;
	struct string_list urls;
	struct string_list_el *p;
	struct _url u;

	if (vzttproxy == NULL)
		return 0;

	string_list_init(&urls);

	if (string_list_read(path, &urls))
		return 0;

	if ((fd = fopen(path, "a")) == NULL)
		return 0;

	for (p = urls.tqh_first; p != NULL; p = p->e.tqe_next) {
		if (parse_url(p->s, &u))
			continue;
		fprintf(fd, "\n%s/%s/%s", vzttproxy, u.server, u.path);
	}
	fclose(fd);
	string_list_clean(&urls);

	return 0;
}

/* 
 Print repositories and mirrorlists into yum config.
 Alternative repositories/mirrorlists separate by space in source records.
 Replace tokens according url_map table.
 If vztt proxy defined dublicate each url:
 proto://user:password@server:port/path -> vzttproxy/server/path
*/
static int yum_print_repos(struct YumTransaction *yum, FILE *fp)
{
	int rc, lfirst, lfound;
	char path[PATH_MAX+1];
	struct repo_rec *r;
	struct _url u;
	struct string_list urls;
	struct string_list_el *p;

	for (r = yum->repositories.tqh_first; r != NULL; r = r->e.tqe_next) {
		string_list_init(&urls);
		if ((rc = env_compat_parse_repo_rec(r->url, yum->url_map,
				&urls, yum->force)))
			return rc;

		/* repoid = setname */
		lfirst = 1;
		fprintf(fp, "[%s%d]\nbaseurl=", r->id, r->num);
		rc = 0;
		for (p = urls.tqh_first; p != NULL; p = p->e.tqe_next) {
			if (!lfirst)
				fprintf(fp, "/\n\t");

			fprintf(fp, "%s", p->s);
			if (yum->vzttproxy) {
				if (parse_url(p->s, &u) == 0)
					fprintf(fp, "/\n\t%s/%s/%s", \
						yum->vzttproxy, u.server, u.path);
			}
			lfirst = 0;
		}
		if (rc)
			return rc;
		fprintf(fp, "\nenabled=1\n");
		if (strstr(yum->basesubdir, "rhel") != NULL) {
			fprintf(fp, "\nsslcacert=/etc/rhel/redhat-uep.pem\n");
			fprintf(fp, "\nsslclientkey=/etc/rhel/sslclientkey.pem\n");
			fprintf(fp, "\nsslclientcert=/etc/rhel/sslclientcert.pem\n");
		}

		if (yum->vzttproxy)
			fprintf(fp, "failovermethod=priority\n");
		string_list_clean(&urls);
	}

	/* process mirrorlists */
	for (r = yum->mirrorlists.tqh_first; r != NULL; r = r->e.tqe_next) {
		string_list_init(&urls);
		if ((rc = env_compat_parse_repo_rec(r->url, yum->url_map,
				&urls, yum->force)))
			return rc;

		if (string_list_empty(&urls))
			continue;

		if ((yum->vzttproxy == NULL) && \
				((string_list_size(&urls) == 1) || \
				(yum->data_source == OPT_DATASOURCE_LOCAL))) {
			/* do not fetch mirrorlist for local mode : 
			   write first mirrorlist at config and exit */
			fprintf(fp, "[%s%d]\nmirrorlist=%s\nenabled=1\n", \
					r->id, r->num, urls.tqh_first->s);
			string_list_clean(&urls);
			continue;
		}
		lfound = 0;
		for (p = urls.tqh_first; p != NULL; p = p->e.tqe_next) {
			/* try to download mirrorlist */
			if (fetch_mirrorlist((struct Transaction *)yum, p->s, path, \
					sizeof(path)) == 0) {
				lfound = 1;
				break;
			}
		}
		string_list_clean(&urls);
		if (!lfound) {
			vztt_logger(0, 0, "Can not load mirrorlists: %s", r->url);
			return VZT_CANT_FETCH;
		}

		/* rewrote fetched mirrorlist file: 
		  dublicate all records for vzttproxy */
		mirrorlist_add_vzttproxy(path, yum->vzttproxy);
		fprintf(fp, "[%s%d]\nmirrorlist=file://%s\n", \
			r->id, r->num, path);
		fprintf(fp, "enabled=1\n");
		if (yum->vzttproxy)
			fprintf(fp, "failovermethod=priority\n");
	}
	return 0;
}

/* create temporary yum config */
static int yum_create_config(struct YumTransaction *yum)
{
	char path[PATH_MAX+1];
	int rc;
	int td;
	FILE *fd;
	struct string_list_el *e;
	int dlevel;

	/* create temporary yum confug */
	snprintf(path, sizeof(path), "%s/yum_conf.XXXXXX", yum->tmpdir);
	if ((td = mkstemp(path)) == -1) {
		vztt_logger(0, errno, "mkstemp(%s) error", path);
		return VZT_CANT_CREATE;
	}
	if ((fd = fdopen(td, "w")) == NULL) {
		close(td);
		vztt_logger(0, errno, "fdopen(%s) error", path);
		return VZT_CANT_OPEN;
	}

	if ((yum->yum_conf = strdup(path)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		fclose(fd);
		close(td);
		return VZT_CANT_ALLOC_MEM;
	}

	fprintf(fd, "[main]\n");
	fprintf(fd, "cachedir=%s/%s/%s\n", \
		yum->tmpldir, yum->basesubdir, yum->datadir);
	/* tolerant ? */
	/* set verbosity
	quiet == 1
		0
	else
		0 - 0
		1 - 1
		2 - 4
		3 - 6
		4 - 8
		5 - 10
	*/
	if (yum->quiet)
		dlevel = 0;
	else {
		if (yum->debug == 0)
			dlevel = 0;
		else {
			dlevel = yum->debug*2;
			/* No sense in > 10 for yum, this will only produce a warning */
			if (dlevel > 10)
            	dlevel = 10;
        }
	}
	fprintf(fd, "debuglevel=%d\n", dlevel);
	fprintf(fd, "errorlevel=%d\n", dlevel);
	fprintf(fd, "reposdir=/etc/vzyum.repos.d\n");
	fprintf(fd, "logfile=%s\n", yum->logfile);
	fprintf(fd, "plugins=1\n");
	fprintf(fd, "timeout=180\n");
	fprintf(fd, "exactarch=1\n");
	fprintf(fd, "metadata_expire=%d\n", METADATA_EXPIRE_MAX);
	fprintf(fd, "installonlypkgs=\n");
	if (!string_list_empty(&yum->exclude)) {
		fprintf(fd, "exclude=");
		for (e = yum->exclude.tqh_first; e != NULL; e = e->e.tqe_next)
			fprintf(fd, " %s", e->s);
		fprintf(fd, "\n");
	}
	if (yum->test)
		fprintf(fd, "tsflags=test\n");

	if ((rc = yum_print_repos(yum, fd))) {
		fclose(fd);
		close(td);
		return rc;
	}
	fclose(fd);
	close(td);
	vztt_logger(2, 0, "Temporary yum config %s was created", yum->yum_conf);
	return 0;
}

static void yum_remove_db(struct YumTransaction *yum)
{
	char buf[PATH_MAX + 1];
	struct stat st;

	snprintf(buf, sizeof(buf) - 1, "%s" VZYUMDB_PATH, yum->rootdir);

	// Absent, do nothing
	if (stat(buf, &st) != 0 && errno == ENOENT)
		return;

	remove_directory(buf);
}

static int yum_remove_config(struct YumTransaction *yum)
{
	if (yum->yum_conf)
	{
	    	unlink(yum->yum_conf);
		free(yum->yum_conf);
		yum->yum_conf = NULL;
	}

	yum_remove_db(yum);

	return 0;
}

/* Run yum from chroot */
static int yum_run(
		struct YumTransaction *yum,
		pm_action_t action,
		struct string_list *packages)
{
	int rc;
	char *cmd = VZYUM_BIN;
	struct string_list args;
	struct string_list envs;
	struct string_list_el *o;
	char buf[PATH_MAX];
	char progress_stage[PATH_MAX];

	/* Empty packages list, special case of app template: #PSBM-26883
	   Not packages-related commands should be executed with packages NULL
	 */
	if (packages && string_list_empty(packages))
		return 0;

	if (yum->pythonpath == NULL) {
		vztt_logger(0, 0, "PYTHONPATH variable not defined");
		return VZT_INTERNAL;
	}
	string_list_init(&args);
	string_list_init(&envs);

	if ((rc = yum_create_config(yum)))
		return rc;

	/* yum parameters */
	yum_set_default_arguments(yum, &args);
	if (yum->outfile) {
		string_list_add(&args, "--outfile");
		string_list_add(&args, yum->outfile);
	}
	if (!EMPTY_CTID(yum->ctid)) {
		string_list_add(&args, "--vps");
		snprintf(buf, sizeof(buf), "%s", yum->ctid);
		string_list_add(&args, buf);
	}
	if (action == VZPKG_GET) {
		string_list_add(&args, "--not-resolve");
		string_list_add(&args, "--download-only");
	} else if (action == VZPKG_FETCH) {
		string_list_add(&args, "--ign-conflicts");
		string_list_add(&args, "--download-only");
	}
	if (yum->vzfs_technologies && !yum->force_openat) {
		string_list_add(&args, "--vzfs3_technologies");
		snprintf(buf, sizeof(buf), "%lu", yum->vzfs_technologies);
		string_list_add(&args ,buf);
	}
	if (yum->force_openat)
		string_list_add(&args, "--force-openat");
	/* additional options */
	for (o = yum->options.tqh_first; o != NULL; o = o->e.tqe_next)
		string_list_add(&args, o->s);

	/* add command */
	switch(action) {
		case VZPKG_UPGRADE:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_DIST_UPGRADE);
			string_list_add(&args, "upgrade");
			break;
		case VZPKG_INSTALL:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_INSTALL);
			string_list_add(&args, "install");
			break;
		case VZPKG_FETCH:
			/* load packages with dependencies resolving, 
			   but ignore conficts */
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_FETCH);
			string_list_add(&args, "install");
			break;
		case VZPKG_GET:
			/* load only specified packages, without any resolving, 
			   ignore all conficts */
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_FETCH);
			string_list_add(&args, "install");
			break;
		case VZPKG_REMOVE:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_REMOVE);
			string_list_add(&args, "remove");
			break;
		case VZPKG_AVAIL:
		case VZPKG_LIST:
			if (yum->debug > 1)
				snprintf(progress_stage, sizeof(progress_stage),
					"%s%s",
					PROGRESS_PKGMAN_PACKAGE_MANAGER,
					PROGRESS_PKGMAN_LIST);
			else
				progress_stage[0] = 0;
			string_list_add(&args, "list");
			/* it is not need - we run yum on empty rpmdb
			args.push_back(strdup("available"));*/
			break;
		case VZPKG_CLEAN_METADATA:
			if (yum->debug > 1)
				snprintf(progress_stage, sizeof(progress_stage),
					"%s%s",
					PROGRESS_PKGMAN_PACKAGE_MANAGER,
					PROGRESS_PKGMAN_CLEAN_METADATA);
			else
				progress_stage[0] = 0;
			string_list_add(&args, "clean");
			string_list_add(&args, "metadata");
			break;
		case VZPKG_MAKECACHE:
			if (yum->debug > 1)
				snprintf(progress_stage, sizeof(progress_stage),
					"%s%s",
					PROGRESS_PKGMAN_PACKAGE_MANAGER,
					PROGRESS_PKGMAN_MAKE_CACHE);
			else
				progress_stage[0] = 0;
			string_list_add(&args, "makecache");
			break;
		case VZPKG_CLEAN:
			snprintf(progress_stage, sizeof(progress_stage),
				"%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_CLEAN);
			string_list_add(&args, "clean");
			string_list_add(&args, "all");
			break;
		case VZPKG_LOCALINSTALL:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_INSTALL);
			string_list_add(&args, "localinstall");
			break;
		case VZPKG_LOCALUPDATE:
			if (yum->expanded)
				string_list_add(&args, "--obsoletes");
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_UPDATE);
			string_list_add(&args, "localupdate");
			break;
		case VZPKG_INFO:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_INFO);
			string_list_add(&args, "info");
			break;
		case VZPKG_GROUPINSTALL:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_GROUP_INSTALL);
			string_list_add(&args, "groupinstall");
			break;
		case VZPKG_GROUPUPDATE:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_GROUP_UPDATE);
			string_list_add(&args, "groupupdate");
			break;
		case VZPKG_GROUPREMOVE:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_GROUP_REMOVE);
			string_list_add(&args, "groupremove");
			break;
		case VZPKG_UPDATE:
		default:
			if (yum->expanded)
			{
				snprintf(progress_stage, sizeof(progress_stage),
					"%s%s",
					PROGRESS_PKGMAN_PACKAGE_MANAGER,
					PROGRESS_PKGMAN_UPGRADE);
				string_list_add(&args, "upgrade");
			}
			else
			{
				snprintf(progress_stage, sizeof(progress_stage),
					"%s%s",
					PROGRESS_PKGMAN_PACKAGE_MANAGER,
					PROGRESS_PKGMAN_UPDATE);
				string_list_add(&args, "update");
			}
			break;
	}

	if (packages) {
		string_list_add(&args, "--allowerasing");
		/* to add packages into arguments */
		for (o = packages->tqh_first; o != NULL; o = o->e.tqe_next)
			string_list_add(&args, o->s);
	}

	/* Enable checker */
	create_veroot_unjump_checker((struct Transaction *)yum, &envs);

	/* export environments */
	string_list_add(&envs, yum->pythonpath);

	/* add proxy in environments */
	if ((rc = add_proxy_env(&yum->http_proxy, HTTP_PROXY, &envs)))
		return rc;
	if ((rc = add_proxy_env(&yum->ftp_proxy, FTP_PROXY, &envs)))
		return rc;
	if ((rc = add_proxy_env(&yum->https_proxy, HTTPS_PROXY, &envs)))
		return rc;

	/* add templates environments too */
	if ((rc = add_tmpl_envs(yum->tdata, &envs)))
		return rc;

	progress(progress_stage, 0, yum->progress_fd);

	/* run cmd from chroot environment */
	if ((rc = run_from_chroot(cmd, yum->envdir, yum->debug,
			yum->ign_pm_err, &args, &envs, yum->osrelease)))
		return rc;

	yum_remove_config(yum);

	/* free mem */
	string_list_clean(&args);
	string_list_clean(&envs);

	progress(progress_stage, 100, yum->progress_fd);

	return 0;
}

/* run yum transaction */
int yum_action(
		struct Transaction *pm,
		pm_action_t action,
		struct string_list *packages)
{
	struct YumTransaction *yum = (struct YumTransaction *)pm;

	return yum_run(yum, action, packages);
}

/* convert osarch to package arch */
char *yum_os2pkgarch(const char *osarch)
{
	if ((strcmp(osarch, ARCH_X86_64) == 0) || \
			(strcmp(osarch, ARCH_IA64) == 0))
		return strdup(osarch);

	return strdup(ARCH_I386);
}

/* move /vz/template/<osname>/<osver>/<osarch>/<repoid>* directories to 
   /vz/template/<osname>/<osver>/<osarch>/yum/ on first start only */
static int yum_move_local_caches(struct YumTransaction *yum)
{
	char path[PATH_MAX+1];
	char buf[PATH_MAX+1];
	DIR * dir;
	char dirent_buf[sizeof(struct dirent) + PATH_MAX + 1];
	struct dirent *de = (struct dirent *) dirent_buf;
	struct dirent *result;
	int retval;
	struct stat st;
	int rc = 0;

	/* check base0 in new path */
	snprintf(buf, sizeof(buf), "%s/%s/%s/" BASEREPONAME "0", \
			yum->tmpldir, yum->basesubdir, yum->datadir);
	if (access(buf, F_OK) == 0)
		return 0;

	snprintf(path, sizeof(path), "%s/%s", yum->tmpldir, yum->basesubdir);
	dir = opendir(path);
	if (!dir) {
		vztt_logger(0, errno, "opendir(\"%s\") error", path);
		return VZT_CANT_OPEN;
	}

	while (1) {
		if ((retval = readdir_r(dir, de, &result)) != 0)
		{
			vztt_logger(0, retval, "readdir_r(\"%s\") error",
					path);
			rc = VZT_CANT_READ;
			break;
		}

		if (result == NULL)
			break;

		if(strcmp(de->d_name, ".") == 0)
			continue;

		if(strcmp(de->d_name, "..") == 0)
			continue;

		snprintf(path, sizeof(path), "%s/%s/%s", yum->tmpldir, \
			yum->basesubdir, de->d_name);
		if (stat(path, &st)) {
			vztt_logger(0, errno, "stat(\"%s\") error", path);
			rc = VZT_CANT_LSTAT;
			break;
		}
		/* is it directory? */
		if (!S_ISDIR(st.st_mode)) continue;

		if(strncmp(de->d_name, BASEREPONAME, strlen(BASEREPONAME)) == 0) {
			char *p;
			int ldig = 1;
			for (p = de->d_name + strlen(BASEREPONAME); *p; p++)
				if (!isdigit(*p))
				{
					ldig = 0;
					break;
				}
			if (!ldig)
				continue;
			/* it is base OS set cache dir */
		} else {
			/* check yum local caches : find headers and packages directories
			- vzpkg clean remove all other files */
			snprintf(buf, sizeof(buf), "%s/headers", path);
			if (stat(buf, &st))
				/* ignore errno != ENOENT */
				continue;
			if (!S_ISDIR(st.st_mode))
				continue;
			snprintf(buf, sizeof(buf), "%s/packages", path);
			if (stat(buf, &st))
				/* ignore errno != ENOENT */
				continue;
			if (!S_ISDIR(st.st_mode))
				continue;
		}

		/* move to new path */
		snprintf(buf, sizeof(buf), "%s/%s/%s/%s", yum->tmpldir, \
				yum->basesubdir, yum->datadir, de->d_name);
		if (access(buf, F_OK) == 0)
			continue;
		if (move_file(buf, path)) {
			vztt_logger(0, 0, "Can not move %s to %s", path, buf);
			break;
		}
	}
 
	closedir(dir);
	return rc;
}

/* find package in local cache 
 dname - directory name in template area (name_version_arch) */
int yum_find_pkg_in_cache(
		struct Transaction *pm,
		const char *dname,
		char *path,
		size_t size)
{
	char buf[PATH_MAX+1];
	char nva[PATH_MAX+1];
	DIR * dir;
	char dirent_buf[sizeof(struct dirent) + PATH_MAX + 1];
	struct dirent *de = (struct dirent *) dirent_buf;
	struct dirent *result;
	int retval;
	struct stat st;
	int rc = 0;
	char *ptr, *p;

	strncpy(nva, dname, sizeof(nva));
	/* remove epoch from package name */
	if ((ptr = strchr(nva, ':'))) {
		for (p = ptr-1; *p != '-' && p > nva; --p);
		if (*p == '-')
			memcpy(p+1, ptr+1, strlen(ptr));
	}
	snprintf(buf, sizeof(buf), "%s/%s/%s", \
		pm->tmpldir, pm->basesubdir, pm->datadir);
	dir = opendir(buf);
	if (!dir) {
		vztt_logger(0, errno, "opendir(%s) error", buf);
		return 0;
	}

	while (1) {
		if ((retval = readdir_r(dir, de, &result)) != 0)
		{
			vztt_logger(0, retval, "readdir_r(\"%s\") error",
					path);
			break;
		}

		if (result == NULL)
			break;

		if(strcmp(de->d_name, ".") == 0)
			continue;

		if(strcmp(de->d_name, "..") == 0)
			continue;

		snprintf(buf, sizeof(buf), "%s/%s/%s/%s", pm->tmpldir, \
			pm->basesubdir, pm->datadir, de->d_name);
		if (stat(buf, &st)) {
			vztt_logger(0, errno, "stat(%s) error", buf);
			continue;
		}
		/* is it directory? */
		if (!S_ISDIR(st.st_mode)) continue;

		snprintf(path, size, "%s/packages/%s.rpm", buf, nva);
		if (access(path, F_OK) == 0) {
			rc = 1;
			break;
		}
	}
	closedir(dir);

	return rc;
}

/*
  get package info from yum, parse and place in structures
*/
int yum_get_info(
		struct Transaction *pm, 
		const char *package, 
		struct pkg_info_list *ls)
{
	int rc;
	struct string_list_el *o;
	struct string_list args;
	struct string_list envs;
	struct YumTransaction *yum = (struct YumTransaction *)pm;

	if (yum->pythonpath == NULL) {
		vztt_logger(0, 0, "PYTHONPATH variable not defined");
		return VZT_INTERNAL;
	}
	string_list_init(&args);
	string_list_init(&envs);

	if ((rc = yum_create_config(yum)))
		return rc;

	yum_set_default_arguments(yum, &args);
	/* additional options */
	for (o = yum->options.tqh_first; o != NULL; o = o->e.tqe_next)
		string_list_add(&args, o->s);
	string_list_add(&args, "info");
	string_list_add(&args, (char *)package);

	/* export environments */
	string_list_add(&envs, yum->pythonpath);

	/* add proxy in environments */
	if ((rc = add_proxy_env(&yum->http_proxy, HTTP_PROXY, &envs)))
		return rc;
	if ((rc = add_proxy_env(&yum->ftp_proxy, FTP_PROXY, &envs)))
		return rc;
	if ((rc = add_proxy_env(&yum->https_proxy, HTTPS_PROXY, &envs)))
		return rc;

	/* add templates environments too */
	if ((rc = add_tmpl_envs(pm->tdata, &envs)))
		return rc;

	/* run cmd from chroot environment */
	if ((rc = run_from_chroot2(VZYUM_BIN, pm->envdir, pm->debug,
		pm->ign_pm_err, &args, &envs, pm->osrelease,
		read_rpm_info, (void *)ls)))
		return rc;

	yum_remove_config(yum);

	/* free mem */
	string_list_clean(&args);
	string_list_clean(&envs);

	if (rc == 0 && pkg_info_list_empty(ls)) {
		/* dpkg-query return 0 for available packages */
		vztt_logger(0, 0, "Packages %s not found", package);
		rc = VZT_PM_FAILED;
	}
	return rc;
}


/* remove per-repositories template cache directories */
int yum_remove_local_caches(struct Transaction *pm, char *reponame)
{
	char path[PATH_MAX+1];
	int i;

	if (reponame == NULL)
		return 0;

	for (i = 0; i < 1000; i++) {
		snprintf(path, sizeof(path), "%s/%s/%s%d", \
			pm->basedir, pm->datadir, reponame, i);
		if (access(path, F_OK))
			break;
		remove_directory(path);
	}
	return 0;
}

/* 
   yum metadata directory: <basedir>/pm/<name><urlno>
   plesk* app template have the same repository set for all template.
   To avoid dubicate metadata, header and packages for such templates,
   vzpkg will replace <basedir>/pm/<name><urlno> directory by symlink to
   directory of first such template.
*/
/* create symlink on app templates template with the same repo set */
int yum_clone_metadata(
		struct Transaction *pm, 
		char *sname, 
		char *dname)
{
	struct repo_rec *r;
	char src[NAME_MAX+1];
	char dst[PATH_MAX+1];

	for (r = pm->repositories.tqh_first; r != NULL; r = r->e.tqe_next) {
		snprintf(dst, sizeof(dst), "%s/" PM_DATA_SUBDIR "%s%d", 
			pm->basedir, dname, r->num);
		snprintf(src, sizeof(src), "%s%d", sname, r->num);
		if ((symlink(src, dst))) {
			return vztt_error(VZT_SYSTEM, errno, 
				"symlink(%s, %s)", src, dst);
		}
	}
	for (r = pm->mirrorlists.tqh_first; r != NULL; r = r->e.tqe_next) {
		snprintf(dst, sizeof(dst), "%s/" PM_DATA_SUBDIR "%s%d", 
			pm->basedir, dname, r->num);
		snprintf(src, sizeof(src), "%s%d", sname, r->num);
		if ((symlink(src, dst))) {
			return vztt_error(VZT_SYSTEM, errno, 
				"symlink(%s, %s)", src, dst);
		}
	}
	return 0;
}

/* scan metadata objects and remove all symlinks
   return != 0 if non-symlink object found */
int yum_clean_metadata_symlinks(
		struct Transaction *pm, 
		char *name)
{
	int rc = 0; 
	struct repo_rec *r;
	char path[PATH_MAX+1];
	struct stat st;

	for (r = pm->repositories.tqh_first; r != NULL; r = r->e.tqe_next) {
		snprintf(path, sizeof(path), "%s/" PM_DATA_SUBDIR "%s%d", 
			pm->basedir, name, r->num);
		if (lstat(path, &st) == 0) {
			if (S_ISLNK(st.st_mode))
				unlink(path);
			else
				rc = 1;
		}
	}
	for (r = pm->mirrorlists.tqh_first; r != NULL; r = r->e.tqe_next) {
		snprintf(path, sizeof(path), "%s/" PM_DATA_SUBDIR "%s%d", 
			pm->basedir, name, r->num);
		if (lstat(path, &st) == 0) {
			if (S_ISLNK(st.st_mode))
				unlink(path);
			else
				rc = 1;
		}
	}
	return rc;
}

/*
Installed Groups:
   Editors
   Legacy Software Development
   ...
Available Groups:
   Administration Tools
   Authoring and Publishing
   ...
Done
*/
#define INSTALLED_GROUPS "Installed Groups:"
#define AVAILABLE_GROUPS "Available Groups:"
#define DONE "Done"

static int read_group_list(FILE *fp, void *data)
{
	char buf[BUFSIZ];
	char *ptr;
	struct string_list installed, available;
	struct string_list *list = NULL;
	struct group_list *groups = (struct group_list *)(data);

	string_list_init(&installed);
	string_list_init(&available);

	while(fgets(buf, sizeof(buf), fp)) {
		if (strncmp(buf, INSTALLED_GROUPS, strlen(INSTALLED_GROUPS)) == 0) {
			list = &installed;
		} else if (strncmp(buf, AVAILABLE_GROUPS, strlen(AVAILABLE_GROUPS)) == 0) {
			list = &available;
		} else if (strncmp(buf, DONE, strlen(DONE)) == 0) {
			list = NULL;
		} else {
			if (!isspace(*buf) && (*buf != '\t'))
				continue;
			if (list == NULL)
				continue;
		
			if ((ptr = cut_off_string(buf)))
				string_list_add(list, ptr);
		}
	}
	string_list_to_array(&available, &groups->available);
	string_list_to_array(&installed, &groups->installed);
	string_list_clean(&available);
	string_list_clean(&installed);

	return 0;
}

/* set yum command line default arguments */
static int yum_set_default_arguments(struct YumTransaction *yum, struct string_list *args)
{
	string_list_add(args, "-y");
	string_list_add(args, "-c");
	string_list_add(args, yum->yum_conf);
	if (yum->data_source == OPT_DATASOURCE_LOCAL)
		string_list_add(args, "-C");
	if (yum->pkgarch) {
		string_list_add(args, "--basearch");
		string_list_add(args, yum->pkgarch);
	}
	if (yum->rootdir) {
		string_list_add(args, "--installroot");
		string_list_add(args, yum->rootdir);
	}
	return 0;
}

/* To get group list from yum, parse and place in structures */
int yum_get_group_list(struct Transaction *pm, struct group_list *groups)
{
	int rc;
	struct string_list_el *o;
	struct string_list args;
	struct string_list envs;
	struct YumTransaction *yum = (struct YumTransaction *)pm;

	if (yum->pythonpath == NULL) {
		vztt_logger(0, 0, "PYTHONPATH variable not defined");
		return VZT_INTERNAL;
	}
	string_list_init(&args);
	string_list_init(&envs);

	if ((rc = yum_create_config(yum)))
		return rc;

	yum_set_default_arguments(yum, &args);
	/* additional options */
	for (o = yum->options.tqh_first; o != NULL; o = o->e.tqe_next)
		string_list_add(&args, o->s);
	string_list_add(&args, "grouplist");
	string_list_add(&args, "hidden");

	/* export environments */
	string_list_add(&envs, yum->pythonpath);

	/* add proxy in environments */
	if ((rc = add_proxy_env(&yum->http_proxy, HTTP_PROXY, &envs)))
		return rc;
	if ((rc = add_proxy_env(&yum->ftp_proxy, FTP_PROXY, &envs)))
		return rc;
	if ((rc = add_proxy_env(&yum->https_proxy, HTTPS_PROXY, &envs)))
		return rc;


	/* add templates environments too */
	if ((rc = add_tmpl_envs(pm->tdata, &envs)))
		goto cleanup;

	/* run cmd from chroot environment */
	rc = run_from_chroot2(
		VZYUM_BIN, pm->envdir, pm->debug, pm->ign_pm_err, &args, &envs,
		yum->osrelease, read_group_list, (void *)groups);

cleanup:
	yum_remove_config(yum);
	string_list_clean(&args);
	string_list_clean(&envs);

	return rc;
}

/*
[root@tvs-win Debug]# yum groupinfo KVM
Loaded plugins: fastestmirror
 * base: mirror.yandex.ru
blah-blah

Group: KVM
 Description: Virtualization Support with KVM
 Mandatory Packages:
   celt051
   ...
 Default Packages:
   libvirt
   ...
 Optional Packages:
   Virtualization-en-US
   ...
[root@tvs-win Debug]#

*/

#define GROUP_TITLE "Group:"

static int read_group_info(FILE *fp, void *data)
{
	char buf[BUFSIZ];
	char *p;
	struct string_list list;
	struct group_info *info = (struct group_info *)(data);

	string_list_init(&list);
	info->name = NULL;
	/* find group name */
	while(fgets(buf, sizeof(buf), fp)) {
		if (strncmp(buf, GROUP_TITLE, strlen(GROUP_TITLE)))
			continue;

		p = cut_off_string(buf + strlen(GROUP_TITLE));
		if (p == NULL) {
			vztt_logger(0, errno, "Cannot find group name in \"%s\"", buf);
			return VZT_CANT_ALLOC_MEM;
		}
		info->name = strdup(p);
		break;
	}
	/* read other text in one list */
	while(fgets(buf, sizeof(buf), fp)) {
		// remove tail 'newline'
		p = buf + strlen(buf) - 1;
		while ((*p == '\n') && p >= buf)
			*p-- = '\0';
		string_list_add(&list, buf);
	}
	/* save data of the group */
	string_list_to_array(&list, &info->list);
	string_list_clean(&list);

	return 0;
}

/* To get group info from yum, parse and place in structures */
int yum_get_group_info(struct Transaction *pm, const char *group, struct group_info *group_info)
{
	int rc;
	struct string_list_el *o;
	struct string_list args;
	struct string_list envs;
	struct YumTransaction *yum = (struct YumTransaction *)pm;

	if (yum->pythonpath == NULL) {
		vztt_logger(0, 0, "PYTHONPATH variable not defined");
		return VZT_INTERNAL;
	}
	string_list_init(&args);
	string_list_init(&envs);

	if ((rc = yum_create_config(yum)))
		return rc;

	yum_set_default_arguments(yum, &args);
	/* additional options */
	for (o = yum->options.tqh_first; o != NULL; o = o->e.tqe_next)
		string_list_add(&args, o->s);
	string_list_add(&args, "groupinfo");
	string_list_add(&args, (char *)group);

	/* export environments */
	string_list_add(&envs, yum->pythonpath);

	/* add proxy in environments */
	if ((rc = add_proxy_env(&yum->http_proxy, HTTP_PROXY, &envs)))
		return rc;
	if ((rc = add_proxy_env(&yum->ftp_proxy, FTP_PROXY, &envs)))
		return rc;
	if ((rc = add_proxy_env(&yum->https_proxy, HTTPS_PROXY, &envs)))
		return rc;

	/* add templates environments too */
	if ((rc = add_tmpl_envs(pm->tdata, &envs)))
		goto cleanup;

	/* run cmd from chroot environment */
	rc = run_from_chroot2(
		VZYUM_BIN, pm->envdir, pm->debug, pm->ign_pm_err, &args, &envs,
		yum->osrelease, read_group_info, (void *)group_info);

cleanup:
	yum_remove_config(yum);
	string_list_clean(&args);
	string_list_clean(&envs);

	return rc;
}
