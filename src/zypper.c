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
 * Our contact details: Parallels IP Holdings GmbH, Vordergasse 59, 8200
 * Schaffhausen, Switzerland.
 *
 * Zypper wrapper
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
#include "zypper.h"
#include "downloader.h"
#include "util.h"
#include "vztt.h"
#include "progress_messages.h"

int zypper_init(struct Transaction *pm);
int zypper_clean(struct Transaction *pm);
int zypper_action(
		struct Transaction *pm,
		pm_action_t action,
		struct string_list *packages);
char *zypper_os2pkgarch(const char *osarch);
int zypper_find_pkg_in_cache(
		struct Transaction *pm,
		const char *dname,
		char *path,
		size_t size);
int zypper_get_info(
		struct Transaction *pm,
		const char *package,
		struct pkg_info_list *ls);
int zypper_remove_local_caches(struct Transaction *pm, char *reponame);
int zypper_clone_metadata(
		struct Transaction *pm,
		char *sname,
		char *dname) { return VZT_INTERNAL; }
int zypper_clean_metadata_symlinks(
		struct Transaction *pm,
		char *name) { return VZT_INTERNAL; }


/* create structure */
int zypper_create(struct Transaction **pm)
{
	*pm = (struct Transaction *)malloc(sizeof(struct ZypperTransaction));
	if (*pm == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory\n");
		return VZT_CANT_ALLOC_MEM;
	}
	memset((void *)(*pm), 0, sizeof(struct ZypperTransaction));
	/* set wrapper functions */
	(*pm)->pm_init = zypper_init;
	(*pm)->pm_clean = zypper_clean;
	(*pm)->pm_get_install_pkg = env_compat_get_install_pkg;
	(*pm)->pm_update_metadata = env_compat_update_metadata;
	(*pm)->pm_action = zypper_action;
	(*pm)->pm_create_root = env_compat_create_root;
	(*pm)->pm_find_pkg_area = env_compat_find_pkg_area;
	(*pm)->pm_find_pkg_area2 = env_compat_find_pkg_area2;
	(*pm)->pm_find_pkg_area_ex = env_compat_find_pkg_area_ex;
	(*pm)->pm_get_int_pkgname = env_compat_get_int_pkgname;
	(*pm)->pm_get_short_pkgname = env_compat_get_short_pkgname;
	(*pm)->pm_fix_pkg_db = env_compat_fix_pkg_db;
	(*pm)->pm_is_std_pkg_area = env_compat_is_std_pkg_area;
	(*pm)->pm_ver_cmp = env_compat_ver_cmp;
	(*pm)->pm_os2pkgarch = zypper_os2pkgarch;
	(*pm)->pm_find_pkg_in_cache = zypper_find_pkg_in_cache;
	(*pm)->pm_pkg_cmp = env_compat_pkg_cmp;
	(*pm)->pm_remove_pkg = env_compat_remove_rpm;
	(*pm)->pm_run_local = env_compat_run_local;
	(*pm)->pm_create_init_cache = env_compat_create_init_cache;
	(*pm)->pm_create_post_init_cache = env_compat_create_post_init_cache;
	(*pm)->pm_create_cache = env_compat_create_cache;
	(*pm)->pm_clean_local_cache = env_compat_clean_local_cache;
	(*pm)->pm_ve_get_info = env_compat_rpm_get_info;
	(*pm)->pm_tmpl_get_info = zypper_get_info;
	(*pm)->pm_remove_local_caches = zypper_remove_local_caches;
	(*pm)->pm_last_repair_fetch = env_compat_last_repair_fetch;
	(*pm)->pm_vzttproxy_fetch = env_compat_vzttproxy_fetch;
	(*pm)->pm_package_find_nevra = env_compat_package_find_nevra;
	/* Not needed for zypper - it do it inside itself */
	(*pm)->pm_clean_metadata_symlinks = zypper_clean_metadata_symlinks;
	(*pm)->pm_clone_metadata = zypper_clone_metadata;
	(*pm)->pm_parse_vzdir_name = env_compat_parse_vzdir_name;
	(*pm)->pm_get_group_list = env_compat_get_group_list;
	(*pm)->pm_get_group_info = env_compat_get_group_info;
	(*pm)->datadir = PM_DATA_SUBDIR;
	(*pm)->pm_type = RPM_ZYPP;

	return 0;
}

/* initialize */
int zypper_init(struct Transaction *pm)
{
	// Empty for now, reserved for SLES credentionals
	struct ZypperTransaction *zypper = (struct ZypperTransaction *)pm;

	if (zypper->data_source == OPT_DATASOURCE_LOCAL)
		vztt_logger(0, 0, "-C/--cache option is not "
			"supported by zypper-maintained ez-templates");
		return VZT_BAD_PARAM;

	return 0;
}

/* cleanup */
int zypper_clean(struct Transaction *pm)
{
	// Empty for now, reserved for SLES credentionals
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

static int zypper_fill_repos(struct ZypperTransaction *zypper,
				struct repo_list *repos,
				char *repotype,
			    	int gpgcheck,
			    	int is_mirrorlist)
{
	char path[PATH_MAX+1];
	int rc, lfound;
	struct repo_rec *r;
	struct string_list urls;
	struct string_list_el *p;
	FILE *fd;

	for (r = repos->tqh_first; r != NULL; r = r->e.tqe_next) {
		string_list_init(&urls);
		if ((rc = env_compat_parse_repo_rec(r->url, zypper->url_map,
				&urls, zypper->force)))
			return rc;

		if (string_list_empty(&urls))
			continue;

		snprintf(path, sizeof(path), "%s/repos.d/%s%d.repo", zypper->tmpdir, r->id, r->num);
		if ((fd = fopen(path, "w")) == NULL) {
			vztt_logger(0, errno, "fdopen(%s) error", path);
			return VZT_CANT_OPEN;
		}

		fprintf(fd, "[%s%d]\n", r->id, r->num);
		fprintf(fd, "name=%s%d\n", r->id, r->num);
		fprintf(fd, "enabled=1\n");
		fprintf(fd, "autorefresh=0\n");
		fprintf(fd, "path=/\n");
		fprintf(fd, "type=%s\n", repotype);
		fprintf(fd, "keeppackages=1\n");
		fprintf(fd, "gpgcheck=%i\n", gpgcheck);
		if (is_mirrorlist) {
			if ((zypper->vzttproxy == NULL) && \
					(string_list_size(&urls) == 1)) {
				/* do not fetch mirrorlist for local mode :
				   write first mirrorlist at config and exit */
				fprintf(fd, "mirrorlist=%s\n", urls.tqh_first->s);
			} else {
				for (p = urls.tqh_first; p != NULL; p = p->e.tqe_next) {
					/* try to download mirrorlist */
					if (fetch_mirrorlist((struct Transaction *)zypper, p->s, path, \
							sizeof(path)) == 0) {
						lfound = 1;
						break;
					}
				}
				if (!lfound) {
					fclose(fd);
					vztt_logger(0, 0, "Can not load mirrorlists: %s", r->url);
					return VZT_CANT_FETCH;
				}
				/* rewrote fetched mirrorlist file:
				  dublicate all records for vzttproxy */
				mirrorlist_add_vzttproxy(path, zypper->vzttproxy);
				fprintf(fd, "mirrorlist=file://%s\n", path);
			}
		} else {
			for (p = urls.tqh_first; p != NULL; p = p->e.tqe_next)
				fprintf(fd, "baseurl=%s\n", p->s);
		}
		string_list_clean(&urls);
		fclose(fd);
	}

	return 0;
}

/*
 Print repositories and mirrorlists into zypper repos directory.
*/
static int zypper_create_repos(struct ZypperTransaction *zypper)
{
	char path[PATH_MAX+1];
	struct stat st;

	snprintf(path, sizeof(path), "%s/repos.d", zypper->tmpdir);
	if (stat(path, &st) == 0) {
		/* is it directory? Remove it! */
		if (S_ISDIR(st.st_mode))
			remove_directory(path);
		else
			unlink(path);
	}

	if (mkdir(path, 0755)) {
		vztt_logger(0, errno, "Can't create %s directory", path);
		return VZT_CANT_CREATE;
	}

	zypper_fill_repos(zypper, &zypper->repositories, "rpm-md", 0, 0);
	zypper_fill_repos(zypper, &zypper->zypp_repositories, "yast2", 1, 0);
	zypper_fill_repos(zypper, &zypper->mirrorlists, "rpm-md", 0, 1);

	return 0;
}

/* create temporary zypper locks file */
static int zypper_create_locks(struct ZypperTransaction *zypper)
{
	char path[PATH_MAX+1];
	FILE *fd;
	struct string_list_el *e;

	snprintf(path, sizeof(path), "%s/locks", zypper->tmpdir);
	if ((fd = fopen(path, "w")) == NULL) {
		vztt_logger(0, errno, "fdopen(%s) error", path);
		return VZT_CANT_OPEN;
	}

	for (e = zypper->exclude.tqh_first; e != NULL; e = e->e.tqe_next) {
		fprintf(fd, "type: package\n");
		fprintf(fd, "solvable_name: %s", e->s);
		fprintf(fd, "match_type: exact\n");
		fprintf(fd, "case_sensitive: on\n");
		fprintf(fd, "\n");
	}

	fclose(fd);
	vztt_logger(2, 0, "Temporary zypper locks config %s/locks was created", zypper->tmpdir);

	return 0;
}

/* create temporary zypper config */
static int zypper_create_config(struct ZypperTransaction *zypper)
{
	char path[PATH_MAX+1];
	struct string_list_el *o;
	struct stat st;
	int rc;
	FILE *fd;

	/* create temporary zypper config */
	snprintf(path, sizeof(path), "%s/zypp.conf", zypper->tmpdir);
	if ((fd = fopen(path, "w")) == NULL) {
		vztt_logger(0, errno, "fdopen(%s) error", path);
		return VZT_CANT_OPEN;
	}

	fprintf(fd, "[main]\n");
	fprintf(fd, "arch = %s\n", zypper_os2pkgarch(zypper->pkgarch));
	fprintf(fd, "cachedir = %s/%s/%s\n", \
		zypper->tmpldir, zypper->basesubdir, zypper->datadir);
	fprintf(fd, "configdir = %s\n",	zypper->tmpdir);
	fprintf(fd, "repo.refresh.delay = %d\n", METADATA_EXPIRE_MAX);
	// Do not use drpms
	fprintf(fd, "download.use_deltarpm = false\n");
	// credentials. TODO: add SLES support
	fprintf(fd, "credentials.global.dir = %s/credentials.d\n",
		zypper->tmpdir);
	fprintf(fd, "credentials.global.file = %s/credentials.cat\n",
		zypper->tmpdir);
	// Use only english in the repos
	fprintf(fd, "repo.refresh.locales = en\n");
	// Log the history to the tempdir
	fprintf(fd, "history.logfile = %s/history\n",
		zypper->tmpdir);

	// VZ options
	if (!EMPTY_CTID(zypper->ctid))
		fprintf(fd, "vz.vps = %s\n", zypper->ctid);
	if (zypper->force_openat)
		fprintf(fd, "vz.force_openat = %i\n", zypper->force_openat);
	if (zypper->outfile)
		fprintf(fd, "vz.outfile = %s\n", zypper->outfile);
	if (zypper->vzfs_technologies)
		fprintf(fd, "vz.technologies = %lu\n", zypper->vzfs_technologies);

	/* additional options */
	for (o = zypper->options.tqh_first; o != NULL; o = o->e.tqe_next)
		fprintf(fd, "%s\n", o->s);

	fclose(fd);

        // Create the locks file
	if (!string_list_empty(&zypper->exclude)) {
		if ((rc = zypper_create_locks(zypper)))
			return rc;
	}

        // Create the repository files
	if ((rc = zypper_create_repos(zypper)))
		return rc;

	// Create empty dirs
	snprintf(path, sizeof(path), "%s/multiversion.d", zypper->tmpdir);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/services.d", zypper->tmpdir);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/vendors.d", zypper->tmpdir);
	mkdir(path, 0755);

	// Create empty credentials TODO: SLES support
	snprintf(path, sizeof(path), "%s/credentials.d", zypper->tmpdir);
	if (stat(path, &st) == 0) {
		/* is it directory? Remove it! */
		if (S_ISDIR(st.st_mode))
			remove_directory(path);
		else
			unlink(path);
	}

	if (mkdir(path, 0755)) {
		vztt_logger(0, errno, "Can't create %s directory", path);
		return VZT_CANT_CREATE;
	}
	snprintf(path, sizeof(path), "%s/credentials.cat", zypper->tmpdir);
	if ((fd = fopen(path, "w")) == NULL) {
		vztt_logger(0, errno, "fdopen(%s) error", path);
		return VZT_CANT_OPEN;
	}
	fclose(fd);

	// Create the systemCheck
	snprintf(path, sizeof(path), "%s/systemCheck", zypper->tmpdir);
	if ((fd = fopen(path, "w")) == NULL) {
		vztt_logger(0, errno, "fdopen(%s) error", path);
		return VZT_CANT_OPEN;
	}
	// Default from OpenSuSE 12.1
	fprintf(fd, "requires:glibc\n");
	fclose(fd);

	vztt_logger(2, 0, "Temporary zypper config dir %s was created", zypper->tmpdir);

	return 0;
}

/* Run zypper from chroot */
static int zypper_run(
		struct ZypperTransaction *zypper,
		pm_action_t action,
		struct string_list *packages)
{
	int rc = 0;
	char *cmd = ZYPPER_BIN;
	struct string_list args;
	struct string_list envs;
	struct string_list_el *o;
	char buf[PATH_MAX];
	char log_dest_path[PATH_MAX+1];
	int fd;
	char progress_stage[PATH_MAX];

	/* Empty packages list, special case of app template: #PSBM-26883
	   Not packages-related commands should be executed with packages NULL
	 */
	if (packages && string_list_empty(packages))
		return 0;

	string_list_init(&args);
	string_list_init(&envs);

	if ((rc = zypper_create_config(zypper)))
		return rc;

	/* zypper parameters */
	if (!zypper->interactive) {
		string_list_add(&args, "-n");
		string_list_add(&args, "--gpg-auto-import-keys");
	}

	string_list_add(&args, "--root");
	// If we does not have the root - we'll use the empty rpmdb provided by vzpkgenv
	if (zypper->rootdir)
		string_list_add(&args, zypper->rootdir);
	else
		string_list_add(&args, zypper->envdir);

	if (zypper->debug > 5) {
		string_list_add(&args, "-vv");
	} else if (zypper->debug > 1) {
		string_list_add(&args, "-v");
	}

	/* add command */
	switch(action) {
		case VZPKG_UPGRADE:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_DIST_UPGRADE);
			string_list_add(&args, "dist-upgrade");
			if (!zypper->interactive)
				string_list_add(&args, "--auto-agree-with-licenses");
			if (zypper->test) {
				string_list_add(&args, "--dry-run");
				// For compatibility with yum
				string_list_add(&args, "--download-only");
			}
			break;
		case VZPKG_INSTALL:
		case VZPKG_LOCALINSTALL:
		case VZPKG_LOCALUPDATE:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_INSTALL);
			string_list_add(&args, "install");
			if (!zypper->interactive)
				string_list_add(&args, "--auto-agree-with-licenses");
			if (zypper->test) {
				string_list_add(&args, "--dry-run");
				// For compatibility with yum
				string_list_add(&args, "--download-only");
			}
			if (zypper->force) {
				string_list_add(&args, "--force");
				string_list_add(&args, "--force-resolution");
			}
			break;
		case VZPKG_FETCH:
		case VZPKG_GET:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_FETCH);
			/* load packages additional options are in zypp.conf */
			string_list_add(&args, "install");
			if (!zypper->interactive)
				string_list_add(&args, "--auto-agree-with-licenses");
			string_list_add(&args, "--dry-run");
			string_list_add(&args, "--download-only");
			break;
		case VZPKG_REMOVE:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_REMOVE);
			string_list_add(&args, "remove");
			// By default zypper checks only package deps and not file deps
			string_list_add(&args, "--force-resolution");
			if (zypper->test) {
				string_list_add(&args, "--dry-run");
				// For compatibility with yum
				string_list_add(&args, "--download-only");
			}
			break;
		case VZPKG_AVAIL:
		case VZPKG_LIST:
			if (zypper->debug > 1)
				snprintf(progress_stage, sizeof(progress_stage),
					"%s%s",
					PROGRESS_PKGMAN_PACKAGE_MANAGER,
					PROGRESS_PKGMAN_LIST);
			else
				progress_stage[0] = 0;
			string_list_add(&args, "packages");
			// Avoid the case if given rpmdb is not empty for some reason
			string_list_add(&args, "--uninstalled-only");
			break;
		case VZPKG_CLEAN_METADATA:
			if (zypper->debug > 1)
				snprintf(progress_stage, sizeof(progress_stage),
					"%s%s",
					PROGRESS_PKGMAN_PACKAGE_MANAGER,
					PROGRESS_PKGMAN_CLEAN_METADATA);
			else
				progress_stage[0] = 0;
			string_list_add(&args, "clean");
			string_list_add(&args, "-m");
			string_list_add(&args, "-M");
			break;
		case VZPKG_MAKECACHE:
			if (zypper->debug > 1)
				snprintf(progress_stage, sizeof(progress_stage),
					"%s%s",
					PROGRESS_PKGMAN_PACKAGE_MANAGER,
					PROGRESS_PKGMAN_MAKE_CACHE);
			else
				progress_stage[0] = 0;
			string_list_add(&args, "refresh");
			break;
		case VZPKG_CLEAN:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_CLEAN);
			string_list_add(&args, "clean");
			break;
		case VZPKG_INFO:
			// Seems like it's not called by vztt at all
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_INFO);
			string_list_add(&args, "info");
			break;
		case VZPKG_GROUPINSTALL:
		case VZPKG_GROUPUPDATE:
		case VZPKG_GROUPREMOVE:
			vztt_logger(0, errno,
				"operations with groups are not supported by zypper");
			rc = VZT_UNSUPPORTED_COMMAND;
			goto cleanup;
		case VZPKG_UPDATE:
		default:
			if (zypper->expanded)
			{
				snprintf(progress_stage, sizeof(progress_stage),
					"%s%s",
					PROGRESS_PKGMAN_PACKAGE_MANAGER,
					PROGRESS_PKGMAN_UPGRADE);
				string_list_add(&args, "dist-upgrade");
			}
			else
			{
				snprintf(progress_stage, sizeof(progress_stage),
					"%s%s",
					PROGRESS_PKGMAN_PACKAGE_MANAGER,
					PROGRESS_PKGMAN_UPDATE);
				string_list_add(&args, "update");
			}
			if (!zypper->interactive)
				string_list_add(&args, "--auto-agree-with-licenses");
			if (zypper->test) {
				string_list_add(&args, "--dry-run");
				// For compatibility with yum
				string_list_add(&args, "--download-only");
			}
			if (zypper->force) {
				string_list_add(&args, "--force-resolution");
			}
			break;
	}

	if (packages) {
		/* to add packages into arguments */
		for (o = packages->tqh_first; o != NULL; o = o->e.tqe_next)
			string_list_add(&args, o->s);
	}

	/* Enable checker */
	create_veroot_unjump_checker((struct Transaction *)zypper, &envs);

	/* export environments */
	string_list_add(&envs,
		"PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin:/root/bin");
	snprintf(buf, sizeof(buf), "ZYPP_CONF=%s/zypp.conf", zypper->tmpdir);
	string_list_add(&envs, buf);
	snprintf(buf, sizeof(buf), "ZYPP_LOGFILE=%s/zypp.log", zypper->tmpdir);
	string_list_add(&envs, buf);
	snprintf(buf, sizeof(buf), "ZYPP_LOCKFILE_ROOT=%s/%s/%s", zypper->tmpldir,
		zypper->basesubdir, zypper->datadir);
	string_list_add(&envs, buf);
	if (zypper->debug > 5)
		string_list_add(&envs, "ZYPP_FULLLOG=1");

	/* add proxy in environments */
	if ((rc = add_proxy_env(&zypper->http_proxy, HTTP_PROXY, &envs)))
		goto cleanup;
	if ((rc = add_proxy_env(&zypper->ftp_proxy, FTP_PROXY, &envs)))
		goto cleanup;
	if ((rc = add_proxy_env(&zypper->https_proxy, HTTPS_PROXY, &envs)))
		goto cleanup;

	/* add templates environments too */
	if ((rc = add_tmpl_envs(zypper->tdata, &envs)))
		goto cleanup;

	progress(progress_stage, 0, zypper->progress_fd);

	/* run cmd from chroot environment */
	rc = run_from_chroot(cmd, zypper->envdir, zypper->debug,
			zypper->ign_pm_err, &args, &envs, zypper->osrelease);

	// Save the generated zypp.log on the high debug level
	if (zypper->debug > 5) {
		snprintf(buf, sizeof(buf), "%s/zypp.log", zypper->tmpdir);
		snprintf(log_dest_path, sizeof(log_dest_path), "/var/log/vztt_zypp.%i.log", getpid());
		if ((fd = open(log_dest_path, O_WRONLY | O_CREAT | O_APPEND, 0644)) == -1) {
			vztt_logger(0, errno, "Can't open %s", buf);
			return VZT_CANT_OPEN;
		}
		if (copy_file_fd(fd, log_dest_path, buf) > 0)
			vztt_logger(0, 0, "Can't save zypp.log");
		else
			vztt_logger(1, 0, "zypp.log saved as %s", log_dest_path);
		close(fd);
	}

	progress(progress_stage, 100, zypper->progress_fd);

cleanup:
	/* free mem */
	string_list_clean(&args);
	string_list_clean(&envs);

	return rc;
}

/* run zypper transaction */
int zypper_action(
		struct Transaction *pm,
		pm_action_t action,
		struct string_list *packages)
{
	struct ZypperTransaction *zypper = (struct ZypperTransaction *)pm;

	switch(action) {
		case VZPKG_FETCH:
			string_list_add(&zypper->options,
				"vz.ign_conflicts = true");
			break;
		case VZPKG_GET:
			string_list_add(&zypper->options,
				"vz.not_resolve = true");
			break;
		default:
			break;
	}

	return zypper_run(zypper, action, packages);
}

/* convert osarch to package arch */
char *zypper_os2pkgarch(const char *osarch)
{
	if (strcmp(osarch, ARCH_X86_64) == 0)
		return strdup(osarch);
	/* FIXME !!! it should be defined in template */
	return strdup(ARCH_I586);
}

static int zypper_find_pkg_in_repos(
		char *directory,
		char *nva,
		int reposdir,
		struct Transaction *pm,
		char *path,
		size_t size)
{
	char buf[PATH_MAX+1];
	DIR * dir;
	char dirent_buf[sizeof(struct dirent) + PATH_MAX + 1];
	struct dirent *de = (struct dirent *) dirent_buf;
	struct dirent *result;
	int retval;
	struct stat st;
	int rc = 0;

	dir = opendir(directory);
	if (!dir) {
		vztt_logger(0, errno, "opendir(%s) error", directory);
		return 0;
	}

	while (1) {
		if ((retval = readdir_r(dir, de, &result)) != 0)
		{
			vztt_logger(0, retval, "readdir_r(\"%s\") error",
					directory);
			break;
		}

		if (result == NULL)
			break;

		if(strcmp(de->d_name, ".") == 0)
			continue;

		if(strcmp(de->d_name, "..") == 0)
			continue;

		snprintf(buf, sizeof(buf), "%s/%s", directory, de->d_name);
		if (stat(buf, &st)) {
			vztt_logger(0, errno, "stat(%s) error", buf);
			continue;
		}

		/* is it directory? */
		if (S_ISDIR(st.st_mode)) {
			if ((rc = zypper_find_pkg_in_repos(buf, nva, 0, pm, path, size))) {
				rc = 1;
				break;
			}
		}

		/* Should it be here? */
		if (!S_ISDIR(st.st_mode) && !reposdir) {
			snprintf(buf, sizeof(buf), "%s/%s.rpm", directory, nva);
			if (access(buf, F_OK) == 0) {
				strncpy(path, buf, size);
				rc = 1;
				break;
			}
		}

	}
	closedir(dir);

	return rc;
}

/* find package in local cache
 dname - directory name in template area (name_version_arch) */
int zypper_find_pkg_in_cache(
		struct Transaction *pm,
		const char *dname,
		char *path,
		size_t size)
{
	char buf[PATH_MAX+1];
	char nva[PATH_MAX+1];
	char *ptr, *p;

	strncpy(nva, dname, sizeof(nva));
	/* remove epoch from package name */
	if ((ptr = strchr(nva, ':'))) {
		for (p = ptr-1; *p != '-' && p > nva; --p);
		if (*p == '-')
			memcpy(p+1, ptr+1, strlen(ptr));
	}
	snprintf(buf, sizeof(buf), "%s/%s/%s/packages", \
		pm->tmpldir, pm->basesubdir, pm->datadir);

	return zypper_find_pkg_in_repos(buf, nva, 1, pm, path, size);
}

/* Read rpm package(s) info from <fp>, parse and
   put into struct pkg_info * list <ls> */
static int zypper_read_rpm_info(FILE *fp, void *data)
{
	char buf[PATH_MAX+1];
	char *str;
	char *delim = 0;
	int is_descr = 0;
	struct pkg_info *p = NULL;
	struct string_list description;
	struct pkg_info_list *ls = (struct pkg_info_list *)data;

	string_list_init(&description);
	while(fgets(buf, sizeof(buf), fp)) {
		if (strncmp(buf, NAME_TITLE, strlen(NAME_TITLE)) == 0) {
			is_descr = 0;
			str = cut_off_string(buf + strlen(NAME_TITLE));
			if (str == NULL)
				continue;
			p = (struct pkg_info *)malloc(sizeof(struct pkg_info));
			if (p == NULL) {
				vztt_logger(0, errno, "Cannot alloc memory");
				return VZT_CANT_ALLOC_MEM;
			}
			p->name = strdup(str);
			p->version = NULL;
			p->release = NULL;
			p->arch = NULL;
			p->summary = NULL;
			p->description = NULL;
			string_list_init(&description);
			pkg_info_list_add(ls, p);
			continue;
		}
		if (p == NULL)
			continue;

		if (strncmp(buf, ARCH_TITLE, \
				strlen(ARCH_TITLE)) == 0) {
			str = cut_off_string(buf + strlen(ARCH_TITLE));
			if (str)
				p->arch = strdup(str);
		}
		else if (strncmp(buf, VERSION_TITLE, \
				strlen(VERSION_TITLE)) == 0) {
			str = cut_off_string(buf + strlen(VERSION_TITLE));
			if (str) {
				if ((delim = strstr(str, "-"))) {
					*delim = 0;
					delim++;
					p->release = strdup(delim);
				}
				p->version = strdup(str);
			}
		}
		else if (strncmp(buf, SUMMARY_TITLE, \
				strlen(SUMMARY_TITLE)) == 0) {
			str = cut_off_string(buf + strlen(SUMMARY_TITLE));
			if (str)
				p->summary = strdup(str);
		}
		else if (strncmp(buf, DESC_TITLE, \
				strlen(DESC_TITLE)) == 0) {
			str = cut_off_string(buf + strlen(DESC_TITLE));
			if (str)
				string_list_add(&description, str);
			is_descr = 1;
		} else if (is_descr) {
			if ((str = cut_off_string(buf)) == NULL) {
				if (p->description == NULL)
					string_list_to_array(&description, \
						&p->description);
				is_descr = 0;
			} else
				string_list_add(&description, str);
		}
	}
	if (p)
		if (p->description == NULL)
			string_list_to_array(&description, &p->description);
	string_list_clean(&description);

	return 0;
}

/*
  get package info from zypper, parse and place in structures
*/
int zypper_get_info(
		struct Transaction *pm,
		const char *package,
		struct pkg_info_list *ls)
{
	char buf[PATH_MAX+1];
	int rc;
	struct string_list args;
	struct string_list envs;
	struct ZypperTransaction *zypper = (struct ZypperTransaction *)pm;

	string_list_init(&args);
	string_list_init(&envs);

	if ((rc = zypper_create_config(zypper)))
		return rc;

	/* zypper parameters */
	if (!zypper->interactive) {
		string_list_add(&args, "-n");
		string_list_add(&args, "--gpg-auto-import-keys");
	}

	string_list_add(&args, "--root");
	// If we does not have the root - we'll use the empty rpmdb provided by vzpkgenv
	if (zypper->rootdir)
		string_list_add(&args, zypper->rootdir);
	else
		string_list_add(&args, zypper->envdir);

	string_list_add(&args, "info");
	string_list_add(&args, (char *)package);

	/* export environments */
	string_list_add(&envs,
		"PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin:/root/bin");
	snprintf(buf, sizeof(buf), "ZYPP_CONF=%s/zypp.conf", zypper->tmpdir);
	string_list_add(&envs, buf);
	snprintf(buf, sizeof(buf), "ZYPP_LOGFILE=%s/zypp.log", zypper->tmpdir);
	string_list_add(&envs, buf);
	snprintf(buf, sizeof(buf), "ZYPP_LOCKFILE_ROOT=%s/%s/%s", zypper->tmpldir,
		zypper->basesubdir, zypper->datadir);
	string_list_add(&envs, buf);
	if (zypper->debug > 5)
		string_list_add(&envs, "ZYPP_FULLLOG=1");

	/* add proxy in environments */
	if ((rc = add_proxy_env(&zypper->http_proxy, HTTP_PROXY, &envs)))
		return rc;
	if ((rc = add_proxy_env(&zypper->ftp_proxy, FTP_PROXY, &envs)))
		return rc;
	if ((rc = add_proxy_env(&zypper->https_proxy, HTTPS_PROXY, &envs)))
		return rc;

	/* add templates environments too */
	if ((rc = add_tmpl_envs(zypper->tdata, &envs)))
		return rc;

	/* run cmd from chroot environment */
	if ((rc = run_from_chroot2(ZYPPER_BIN, pm->envdir, pm->debug,
		pm->ign_pm_err, &args, &envs, pm->osrelease,
		zypper_read_rpm_info, (void *)ls)))
		return rc;

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
int zypper_remove_local_caches(struct Transaction *pm, char *reponame)
{
	char path[PATH_MAX+1];
	int i;

	if (reponame == NULL)
		return 0;

	for (i = 0; i < 1000; i++) {
		snprintf(path, sizeof(path), "%s/%s/raw/%s%d", \
			pm->basedir, pm->datadir, reponame, i);
		if (!access(path, F_OK))
			remove_directory(path);
		snprintf(path, sizeof(path), "%s/%s/packages/%s%d", \
			pm->basedir, pm->datadir, reponame, i);
		if (!access(path, F_OK))
			remove_directory(path);
		snprintf(path, sizeof(path), "%s/%s/solv/%s%d", \
			pm->basedir, pm->datadir, reponame, i);
		if (!access(path, F_OK))
			remove_directory(path);
	}
	return 0;
}

