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
 * cache operations module
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
#include <dirent.h>
#include <error.h>
#include <limits.h>
#include <signal.h>
#include <sys/wait.h>
#include <utime.h>
#include <asm/unistd.h>
#include <sys/mount.h>

#include <vzctl/libvzctl.h>
#include <vzctl/vzerror.h>

#include "vztt.h"
#include "util.h"
#include "config.h"
#include "tmplset.h"
#include "lock.h"
#include "ploop.h"
#include "cache.h"
#include "progress_messages.h"

#define CACHE_INIT_BIN "vztt/myinit"
#define PFCACHE_BIN "/usr/libexec/vztt_pfcache_xattr"

/* cache creation mode */
#define  OPT_CACHE_FAIL_EXISTED  0
#define  OPT_CACHE_SKIP_EXISTED  1
#define  OPT_CACHE_RECREATE      2

/* create os template cache */
static int create_cache(
	char *ostemplate,
	int mode,
	struct options_vztt *opts_vztt)
{
	int rc = 0;
	char path[PATH_MAX+1];
	char cmd[2*PATH_MAX+1];
	struct sigaction act_int;
	ctid_t ctid;
	char *ve_root = NULL;
	char *ve_private = NULL;
	char *ve_config = NULL;
	char *myinit = NULL;
	char *pwd = NULL;
	char *cachename = NULL;
	char *ploop_dir = NULL;
	char *ve_private_template = NULL;
	FILE *fp;
	void *lockdata, *velockdata, *cache_lockdata;
	char vzfs[MAXVERSIONLEN];
	int backup;

	struct package_list installed;

	struct string_list packages0;
	struct string_list packages1;
	struct string_list packages;

	struct global_config gc;
	struct vztt_config tc;

	struct tmpl_set *tmpl;
	struct Transaction *to = NULL;

	progress(PROGRESS_CREATE_CACHE, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	package_list_init(&installed);
	string_list_init(&packages0);
	string_list_init(&packages1);
	string_list_init(&packages);

	if (getcwd(path, sizeof(path)))
		pwd = strdup(path);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* Check for vefstype all */
	if (gc.veformat == 0)
	{
		vztt_logger(0, 0, "Unsupported file system (vefstype): all");
		return VZT_BAD_PARAM;
	}

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	snprintf(path, sizeof(path), "%s/cache", gc.template_dir);
	if (access(path, F_OK)) {
		if (mkdir(path, 0755)) {
			vztt_logger(0, errno, "mkdir(%s) error", path);
			return VZT_CANT_CREATE;
		}
	}

	if (access(ENV_CONF_DIR CACHE_VE_CONF, F_OK)) {
		vztt_logger(0, 0, ENV_CONF_DIR CACHE_VE_CONF " not found");
		return VZT_TCACHE_CONF_NFOUND;
	}

	if (access(LIB64DIR CACHE_INIT_BIN, X_OK) == 0)
		myinit = LIB64DIR CACHE_INIT_BIN;
	else if (access(LIBDIR CACHE_INIT_BIN, X_OK) == 0)
		myinit = LIBDIR CACHE_INIT_BIN;
	else {
		vztt_logger(0, 0, "init executable not found");
		return VZT_TCACHE_INIT_NFOUND;
	}

	/* load corresponding os template in configs directory */
	if ((rc = tmplset_load(gc.template_dir, ostemplate, NULL, 0, &tmpl,
			opts_vztt->flags)))
		return rc;

	/* Check for cache_type supported */
	if ((tmpl->base->cache_type & get_cache_type(&gc)) == 0) {
		vztt_logger(0, 0, "The template is not compatible with the " \
			"VEFSTYPE used");
		return VZT_TMPL_BROKEN;
	}

	tmpl_get_cache_tar_name(path, sizeof(path), tc.archive,
				get_cache_type(&gc), gc.template_dir, tmpl->os->name);
	if ((cachename = strdup(path)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup_0;
	}

	if ((rc = cache_lock(&gc, cachename, LOCK_WRITE, opts_vztt->flags,
			&cache_lockdata)))
		goto cleanup_0;

	if ((access(cachename, F_OK) == 0) &&
		!(opts_vztt->flags & OPT_VZTT_FORCE)) {
		if (mode == OPT_CACHE_SKIP_EXISTED) {
			vztt_logger(1, 0, "%s cache file already exist",
					tmpl->os->name);
			rc = 0;
			goto cleanup_unlock_cache;
		} else if (mode != OPT_CACHE_RECREATE) {
			vztt_logger(0, 0, "%s cache file already exist",
					tmpl->os->name);
			rc = VZT_TCACHE_EXIST;
			goto cleanup_unlock_cache;
		}
	}

	/* Skip create just convert image if old cache exist */
	if (get_cache_type(&gc) & VZT_CACHE_TYPE_PLOOP_V2) {
		char old[PATH_MAX+1];
		tmpl_get_cache_tar_name(old, sizeof(old), tc.archive,
				VZT_CACHE_TYPE_SIMFS | VZT_CACHE_TYPE_PLOOP,
				gc.template_dir, tmpl->os->name);

		if (access(old, F_OK) == 0) {
			rc = convert_ploop(old, cachename, opts_vztt);
			if (mode != OPT_CACHE_RECREATE || rc != 0)
				goto cleanup_unlock_cache;
		}
	}

	/* check & update metadata */
	if ((rc = update_metadata(ostemplate , &gc, &tc, opts_vztt)))
		goto cleanup_unlock_cache;

	progress(PROGRESS_CREATE_TEMP_CONTAINER, 0, opts_vztt->progress_fd);

	tmplset_mark(tmpl, NULL, TMPLSET_MARK_OS, NULL);
	if (string_list_empty(&tmpl->os->packages0)) {
		if ((rc = string_list_copy(&packages0, &tmpl->base->packages0)))
			goto cleanup_unlock_cache;
	} else {
		if ((rc = string_list_copy(&packages0, &tmpl->os->packages0)))
			goto cleanup_unlock_cache;
	}
	if (string_list_empty(&tmpl->os->packages1)) {
		if ((rc = string_list_copy(&packages1, &tmpl->base->packages1)))
			goto cleanup_unlock_cache;
	} else {
		if ((rc = string_list_copy(&packages1, &tmpl->os->packages1)))
			goto cleanup_unlock_cache;
	}
	if ((rc = string_list_copy(&packages, &tmpl->os->packages)))
		goto cleanup_unlock_cache;

	/* ignore C-c because of cleanup */
	sigaction(SIGINT, NULL, &act_int);
	signal(SIGINT, SIG_IGN);

	if ((rc = lock_free_veid(opts_vztt->flags, ctid , &velockdata)))
		goto cleanup_1;

	/* copy cache sample config to ctid.conf */
	snprintf(path, sizeof(path), ENV_CONF_DIR "%s.conf", ctid);
	if ((ve_config = strdup(path)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup_1_1;
	}
	if ((rc = copy_file(ve_config, ENV_CONF_DIR CACHE_VE_CONF)))
		goto cleanup_1_1;

	/* create & init package manager wrapper */
	if ((rc = pm_init(ctid, &gc, &tc, tmpl, opts_vztt, &to)))
		goto cleanup_2;

	/* set VZFS technologies set according veformat */
	if ((rc = pm_set_veformat(to, gc.veformat)))
		goto cleanup_3;

	/* add global custom exclude */
	if ((rc = pm_add_exclude(to, tc.exclude)))
		goto cleanup_3;

	/* create temporary root/private directory */
	snprintf(path, sizeof(path), "%s/cache-root", to->tmpdir);
	if ((ve_root = strdup(path)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup_3;
	}
	if (mkdir(ve_root, 0755)) {
		vztt_logger(0, errno, "mkdir(%s) error", ve_root);
		rc = VZT_CANT_CREATE;
		goto cleanup_3;
	}
	snprintf(path, sizeof(path), "%s/cache-private", to->tmpdir);
	if ((ve_private = strdup(path)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup_3;
	}

	if (mkdir(ve_private, 0755) != 0) {
		vztt_logger(0, 0, "Failed to create %s dir", path);
		rc = VZT_CANT_CREATE;
		goto cleanup_3;
	}

	/* add ve_root & ve_private into ve config */
	if ((fp = fopen(ve_config, "a")) == NULL) {
		vztt_logger(0, errno, "fopen(%s) error", ve_config);
		rc = VZT_CANT_OPEN;
		goto cleanup_3;
	}
	fprintf(fp, "VE_PRIVATE=%s\n", ve_private);
	fprintf(fp, "VE_ROOT=%s\n", ve_root);
	fprintf(fp, "OSTEMPLATE=.%s\n", tmpl->os->name);
	fclose(fp);

	if (gc.velayout == VZT_VE_LAYOUT5)
	{
		//ploop mode
		struct ve_config vc;

		if ((rc = create_ploop_dir(ve_private, &ploop_dir)))
			goto cleanup_3;

		/*read CT from config*/
		ve_config_init(&vc);
		if ((rc = ve_config_read(ctid, &gc, &vc, 0)))
			return rc;

		/*create ploop device*/
		if ((rc = create_ploop(ploop_dir, vc.diskspace, opts_vztt))) {
			vztt_logger(0, 0, "Cannot create ploop device");
			ve_config_clean(&vc);
			rc = VZT_CANT_ALLOC_MEM;
			goto cleanup_3;
		}

		/*clean config*/
		ve_config_clean(&vc);

		/*disable quota*/
		if ((fp = fopen(ve_config, "a")) == NULL) {
			vztt_logger(0, errno, "fopen(%s) error", ve_config);
			rc = VZT_CANT_OPEN;
			goto cleanup_3;
		}
		fprintf(fp, "DISK_QUOTA=no\n");
		fclose(fp);
	}

	if ((rc = pm_set_root_dir(to, ve_root)))
		goto cleanup_3;

	if ((rc = vefs_get_link(gc.veformat, vzfs, sizeof(vzfs))))
		goto cleanup_3;

	/* Create .ve.layout symlink */
	if ((rc = create_ve_layout(gc.velayout, ve_private)))
		goto cleanup_3;

	snprintf(path, sizeof(path), "%s/templates", ve_private);
	if ((ve_private_template = strdup(path)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup_3;
	}
	if (mkdir(ve_private_template, 0755)) {
		vztt_logger(0, errno, "mkdir(%s) error", path);
		rc = VZT_CANT_CREATE;
		goto cleanup_3;
	}

	/* cache will created only in vz3 layout */
	snprintf(cmd, sizeof(cmd),
		VZCTL " --skiplock %s mount %s --skip_ve_setup",
		opts_vztt->debug < 4 ? "--quiet" : "--verbose", ctid);
	vztt_logger(2, 0, "system(\"%s\")", cmd);
	if ((rc = system(cmd)) == -1) {
		vztt_logger(0, errno, "system(%s) error", cmd);
		rc = VZT_CANT_EXEC;
		goto cleanup_3;
	}
	if (WEXITSTATUS(rc)) {
		vztt_logger(0, 0, "\"%s\" return %d", cmd, WEXITSTATUS(rc));
		rc = VZT_CMD_FAILED;
		goto cleanup_3;
	}

	if (ploop_dir) {
		/* set 'trusted' xattr on CT root, let's kernel will calculate
		   checksum for all files in CT (https://jira.sw.ru/browse/PSBM-10447) */
		snprintf(cmd, sizeof(cmd), PFCACHE_BIN " set %s", ve_root);
		vztt_logger(2, 0, "system(\"%s\")", cmd);
		if (system(cmd)) {
			rc = vztt_error(VZT_CANT_EXEC, errno, "system(%s) error", cmd);
			goto cleanup_4;
		}
	}

	snprintf(path, sizeof(path), "%s/sbin", ve_root);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/dev", ve_root);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/dev/null", ve_root);
	if (mknod(path, S_IFCHR|0666, makedev(1, 3))) {
		vztt_logger(0, errno, "Can create %s", path);
		rc = VZT_CANT_CREATE;
		goto cleanup_4;
	}
	snprintf(path, sizeof(path), "%s/sbin/init", ve_root);
	if ((rc = copy_file(path, myinit)))
		goto cleanup_4;

	if ((rc = to->pm_create_root(ve_root)))
		goto cleanup_4;

	progress(PROGRESS_CREATE_TEMP_CONTAINER, 100, opts_vztt->progress_fd);

/* TODO: check_vzfs_mnt $rootdir */
	/* lock template area on read */
	if ((rc = tmpl_lock(&gc, tmpl->base,
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_4;

	/* Call pre-cache script in VE0 context. */
	if ((rc = tmplset_run_ve0_scripts(tmpl, ve_root, ctid, "pre-cache", 0,
		opts_vztt->progress_fd)))
		goto cleanup_5;

	snprintf(cmd, sizeof(cmd),
		VZCTL " --skiplock %s start %s --skip_ve_setup",
		opts_vztt->debug < 4 ? "--quiet" : "--verbose", ctid);
	vztt_logger(2, 0, "system(\"%s\")", cmd);
	if ((rc = system(cmd)) == -1) {
		vztt_logger(0, errno, "system(%s) error", cmd);
		rc = VZT_CANT_EXEC;
		goto cleanup_5;
	}
	if (WEXITSTATUS(rc) == VZCTL_E_NO_LICENSE) {
		vztt_logger(0, 0, "VZ license not loaded, or invalid class ID");
		rc = VZT_NO_LICENSE;
		goto cleanup_5;
	}
	else if (WEXITSTATUS(rc)) {
		vztt_logger(0, 0, "\"%s\" return %d", cmd, WEXITSTATUS(rc));
		rc = VZT_CMD_FAILED;
		goto cleanup_5;
	}

	/* Check for mid-install script, currently used in Ubuntu 10.10 */
	snprintf(cmd, sizeof(cmd), "%s/mid-pre-install", tmpl->os->confdir);
	if (access(cmd, X_OK) == 0) {
		rc = to->pm_create_init_cache(to, &packages0, &packages1, &packages,
			&installed);
		if (rc)
			goto cleanup_6;

		progress(PROGRESS_RESTART_CONTAINER, 0, opts_vztt->progress_fd);

		if ((rc = tmplset_run_ve_scripts(tmpl, ctid, ve_root,
			"mid-pre-install", 0, opts_vztt->progress_fd)))
			goto cleanup_6;

		/* Restart VPS */
		snprintf(cmd, sizeof(cmd),
			VZCTL " --skiplock %s stop %s --fast",
			opts_vztt->debug < 4 ? "--quiet" : "--verbose", ctid);
		vztt_logger(2, 0, "system(\"%s\")", cmd);
		if ((rc = system(cmd)) == -1) {
			vztt_logger(0, errno, "system(%s) error", cmd);
			rc = VZT_CANT_EXEC;
			goto cleanup_6;
		}
		if (WEXITSTATUS(rc)) {
			vztt_logger(0, 0, "\"%s\" return %d", cmd, WEXITSTATUS(rc));
			rc = VZT_CMD_FAILED;
			goto cleanup_6;
		}

		snprintf(cmd, sizeof(cmd),
			VZCTL " --skiplock %s start %s --skip_ve_setup",
			opts_vztt->debug < 4 ? "--quiet" : "--verbose", ctid);
		vztt_logger(2, 0, "system(\"%s\")", cmd);
		if ((rc = system(cmd)) == -1) {
			vztt_logger(0, errno, "system(%s) error", cmd);
			rc = VZT_CANT_EXEC;
			goto cleanup_6;
		}
		if (WEXITSTATUS(rc)) {
			vztt_logger(0, 0, "\"%s\" return %d", cmd, WEXITSTATUS(rc));
			rc = VZT_CMD_FAILED;
			goto cleanup_6;
		}

		/* Do not run mid-post-install if it does not exist */
		snprintf(cmd, sizeof(cmd), "%s/mid-post-install", tmpl->os->confdir);
		if (access(cmd, X_OK) == 0 && (rc = tmplset_run_ve_scripts(tmpl, ctid, ve_root,
			"mid-post-install", 0, opts_vztt->progress_fd)))
			goto cleanup_6;

		progress(PROGRESS_RESTART_CONTAINER, 100, opts_vztt->progress_fd);

		rc = to->pm_create_post_init_cache(to, &packages0, &packages1, &packages, &installed);

	}
	else {
		rc = to->pm_create_cache(to, &packages0, &packages1, &packages, &installed);
	}

	if (ploop_dir) {
		/* clear 'trusted' xattr for directories, missing in white list */
		snprintf(cmd, sizeof(cmd), PFCACHE_BIN " clear %s", ve_root);
		vztt_logger(2, 0, "system(\"%s\")", cmd);
		if (system(cmd)) {
			rc = vztt_error(VZT_CANT_EXEC, errno, "system(%s) error", cmd);
			goto cleanup_6;
		}
	}

	if (opts_vztt->flags & OPT_VZTT_TEST) {
		printf("cache completed, rc = %d\n", rc);
		printf("ctid %s, ve_root %s, ve_private %s\n",
			ctid, ve_root, ve_private);
		printf("press any key to continue\n");
		getchar();
	}
	if (rc)
		goto cleanup_6;

	/* Call post-cache script in VE0 context. */
 	if ((rc = tmplset_run_ve0_scripts(tmpl, ve_root, ctid, "post-cache", 0,
 		opts_vztt->progress_fd)))
		goto cleanup_6;

	/* Call post-install script in VE context. */
 	if ((rc = tmplset_run_ve_scripts(tmpl, ctid, ve_root, "post-install", 0,
 		opts_vztt->progress_fd)))
		goto cleanup_6;

	if ((rc = save_vzpackages(ve_private, &installed)))
		goto cleanup_6;

	tmpl_unlock(lockdata, opts_vztt->flags);

	snprintf(cmd, sizeof(cmd), VZCTL " --skiplock --quiet stop %s --fast",\
			ctid);
	if ((rc = exec_cmd(cmd, (opts_vztt->flags & OPT_VZTT_QUIET))))
		goto cleanup_4;

	tmplset_update_privdir(tmpl, ve_private);

	if (ploop_dir) {
		/*resize - ignore exit code*/
		resize_ploop(ploop_dir, opts_vztt, 0);
	}

	progress(PROGRESS_PACK_CACHE, 0, opts_vztt->progress_fd);

	/* do not rewrote tarball for test mode */
	if (opts_vztt->flags & OPT_VZTT_TEST)
		goto cleanup_3;

        /* Create tarball */
	snprintf(path, sizeof(path), "%s-old", cachename);
	if (access(cachename, F_OK) == 0) {
		move_file(path, cachename);
		backup = 1;
	} else {
		backup = 0;
	}

	/* Check for another archiver type and remove it too */
	if (tmpl_get_cache_tar(&gc, path, sizeof(path), gc.template_dir,
		tmpl->os->name) == 0)
		unlink(path);

	if (ploop_dir) {
		/*move 'templates' to directory with ploop device. it should be packed
		 together with ploop */
		snprintf(cmd, sizeof(cmd),
			"mv  %s  %s ", ve_private_template, ploop_dir);

		if ((rc = exec_cmd(cmd, (opts_vztt->flags & OPT_VZTT_QUIET))))
			vztt_logger(0, 0, "Failed to move 'template' directory");
		else
		{
			remove_directory(ve_root);
			/*pack ploop device to archive*/
			rc = pack_ploop(ploop_dir, cachename, opts_vztt);
		}
	} else {
		if (chdir(ve_private) == -1) {
			vztt_logger(0, errno, "chdir(%s) failed", ve_private);
			if (backup)
				move_file(cachename, path);
			goto cleanup_3;
		}
		get_pack_cmd(cmd, sizeof(cmd), cachename, ".", " --numeric-owner");
		rc = exec_cmd(cmd, (opts_vztt->flags & OPT_VZTT_QUIET));
		if (pwd)
			if (chdir(pwd) == -1)
				vztt_logger(0, errno, "chdir(%s) failed", pwd);
	}

	if (rc) {
		unlink(cachename);
		/* restore old cache if failed */
		if (backup)
			move_file(cachename, path);
		goto cleanup_3;
	} else {
		if (backup)
			unlink(path);
	}

	progress(PROGRESS_PACK_CACHE, 100, opts_vztt->progress_fd);

	goto cleanup_2;
cleanup_6:
	tmpl_unlock(lockdata, opts_vztt->flags);
	snprintf(cmd, sizeof(cmd), VZCTL " --skiplock --quiet stop %s --fast",
			ctid);
	if (system(cmd) == -1)
		vztt_logger(0, errno, "system(%s) failed", cmd);
	goto cleanup_3;
cleanup_5:
	tmpl_unlock(lockdata, opts_vztt->flags);
cleanup_4:
	snprintf(cmd, sizeof(cmd), VZCTL " --skiplock --quiet umount %s",
			ctid);
	if (system(cmd) == -1)
		vztt_logger(0, errno, "system(%s) failed", cmd);

cleanup_3:
	if (ploop_dir)
		umount_ploop(ploop_dir, opts_vztt);
cleanup_2:
	if(ve_config)
		unlink(ve_config);

	pm_clean(to);
  cleanup_1_1:
	unlock_ve(ctid, velockdata, opts_vztt->flags);
cleanup_1:
	sigaction(SIGINT, &act_int, NULL);
cleanup_unlock_cache:
	cache_unlock(cache_lockdata, opts_vztt->flags);
cleanup_0:
	tmplset_clean(tmpl);

	VZTT_FREE_STR(ve_private_template);
	VZTT_FREE_STR(ploop_dir);
	VZTT_FREE_STR(ve_root);
	VZTT_FREE_STR(ve_private);
	if (ve_config)
		free((void *)ve_config);

	VZTT_FREE_STR(cachename);
	package_list_clean(&installed);
	string_list_clean(&packages0);
	string_list_clean(&packages1);
	string_list_clean(&packages);
	global_config_clean(&gc);
	vztt_config_clean(&tc);

	progress(PROGRESS_CREATE_CACHE, 100, opts_vztt->progress_fd);

	return rc;
}

int update_cache(
	char *ostemplate,
	struct options_vztt *opts_vztt)
{
	int rc = 0;
	char path[PATH_MAX+1];
	char cmd[2*PATH_MAX+1];
	struct sigaction act;
	struct sigaction act_int;
	char *ve_root = NULL;
	char *ve_private = NULL;
	char *ve_config = NULL;
	char *pwd = NULL;
	char *cachename = NULL;
	char *ploop_dir = NULL;
	char *ve_private_template = NULL;
	FILE *fp;
	unsigned veformat;
	void *lockdata, *velockdata;
	int backup;

	struct package_list installed;

	struct string_list args;
	struct string_list packages;
	struct string_list_el *p;

	struct global_config gc;
	struct vztt_config tc;

	struct tmpl_set *tmpl;
	struct Transaction *to;
	ctid_t ctid;

	progress(PROGRESS_UPDATE_CACHE, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	package_list_init(&installed);
	string_list_init(&packages);
	string_list_init(&args);

	if (getcwd(path, sizeof(path)))
		pwd = strdup(path);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* Check for vefstype all */
	if (gc.veformat == 0)
	{
		vztt_logger(0, 0, "Unsupported file system (vefstype): all");
		return VZT_BAD_PARAM;
	}

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;


	if (access(ENV_CONF_DIR CACHE_VE_CONF, F_OK)) {
		vztt_logger(0, 0, ENV_CONF_DIR CACHE_VE_CONF " not found");
		return VZT_TCACHE_CONF_NFOUND;
	}

	/* check & update metadata */
	if ((rc = update_metadata(ostemplate , &gc, &tc, opts_vztt)))
		return rc;

	progress(PROGRESS_CREATE_TEMP_CONTAINER, 0, opts_vztt->progress_fd);

	/* load corresponding os template in configs directory */
	if ((rc = tmplset_load(gc.template_dir, ostemplate, NULL, 0, &tmpl,
			opts_vztt->flags)))
		return rc;

	/* if cache file does not exist - run create_cache */
	tmpl_get_cache_tar_name(path, sizeof(path), tc.archive,
				get_cache_type(&gc), gc.template_dir, tmpl->os->name);
	if (access(path, F_OK) != 0)
	{
		/* Recreate cache here for the old-ploop format case */
		rc = create_cache(tmpl->os->name, OPT_CACHE_RECREATE, opts_vztt);
		goto cleanup_0;
	}

	if ((cachename = strdup(path)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup_0;
	}

	tmplset_mark(tmpl, NULL, TMPLSET_MARK_OS, NULL);
	tmplset_get_marked_pkgs(tmpl, &packages);

	sigaction(SIGINT, NULL, &act_int);
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = SIG_IGN;
	sigaction(SIGINT, &act, NULL);

	if ((rc = lock_free_veid(opts_vztt->flags, ctid, &velockdata)))
		goto cleanup_1;

	/* copy cache sample config to ctid.conf */
	snprintf(path, sizeof(path), ENV_CONF_DIR "%s.conf", ctid);
	if ((ve_config = strdup(path)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup_1_1;
	}
	if ((rc = copy_file(ve_config, ENV_CONF_DIR CACHE_VE_CONF)))
		goto cleanup_1_1;

	/* create & init package manager wrapper */
	if ((rc = pm_init(ctid, &gc, &tc, tmpl, opts_vztt, &to)))
		goto cleanup_2;

	/* add global custom exclude */
	if ((rc = pm_add_exclude(to, tc.exclude)))
		goto cleanup_3;

	/* create temporary root/private directory */
	snprintf(path, sizeof(path), "%s/cache-root", to->tmpdir);
	if ((ve_root = strdup(path)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup_3;
	}
	if (mkdir(ve_root, 0755)) {
		vztt_logger(0, errno, "mkdir(%s) error", ve_root);
		rc = VZT_CANT_CREATE;
		goto cleanup_3;
	}
	snprintf(path, sizeof(path), "%s/cache-private", to->tmpdir);
	if ((ve_private = strdup(path)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup_3;
	}
	if (mkdir(ve_private, 0700)) {
		vztt_logger(0, errno, "mkdir(%s) error", ve_private);
		rc = VZT_CANT_CREATE;
		goto cleanup_3;
	}
	/* add ve_root & ve_private into ve config */
	if ((fp = fopen(ve_config, "a")) == NULL) {
		vztt_logger(0, errno, "fopen(%s) error", ve_config);
		rc = VZT_CANT_OPEN;
		goto cleanup_3;
        }
	fprintf(fp, "VE_PRIVATE=%s\n", ve_private);
	fprintf(fp, "VE_ROOT=%s\n", ve_root);
	fprintf(fp, "OSTEMPLATE=%s\n", tmpl->os->name);
	fclose(fp);

	if ((rc = pm_set_root_dir(to, ve_root)))
		goto cleanup_3;

	/* get veformat from old cache */
	if ((veformat = vzctl2_get_veformat(ve_private)) == -1)
		veformat = VZ_T_VZFS0;

	/* Create .ve.layout symlink */
	if ((rc = create_ve_layout(gc.velayout, ve_private)))
		goto cleanup_3;

	if (gc.velayout == VZT_VE_LAYOUT5)
	{
		if ((rc = create_ploop_dir(ve_private, &ploop_dir)))
			goto cleanup_3;

		/*disable quota*/
		if ((fp = fopen(ve_config, "a")) == NULL) {
			vztt_logger(0, errno, "fopen(%s) error", ve_config);
			rc = VZT_CANT_OPEN;
			goto cleanup_3;
		}
		fprintf(fp, "DISK_QUOTA=no\n");
		fclose(fp);

		vztt_logger(1, 0, "Unpacking ploop %s", cachename);
		get_unpack_cmd(cmd, sizeof(cmd), cachename, ploop_dir, "");
		if ((rc = exec_cmd(cmd, (opts_vztt->flags & OPT_VZTT_QUIET))))
			goto cleanup_3;

		/* Get the required the ploop size */
		struct ve_config vc;

		/* read info from config */
		ve_config_init(&vc);
		if ((rc = ve_config_read(ctid, &gc, &vc, 0)))
			goto cleanup_3;

		if ((rc = resize_ploop(ploop_dir, opts_vztt,
			vc.diskspace)))
		{
			ve_config_clean(&vc);
			vztt_logger(0, 0, "Cannot resize ploop device");
			goto cleanup_3;
		}

		/* clean config */
		ve_config_clean(&vc);

		/*mounting ploop*/
		if ((rc = mount_ploop(ploop_dir, ve_root, opts_vztt))) {
			vztt_logger(0, 0, "Cannot mount ploop device");
			rc = VZT_CANT_ALLOC_MEM;
			goto cleanup_3;
		}

		/*create link 'templates' in private directory */
		snprintf(path, sizeof(path), "%s/templates", ve_private);
		if ((ve_private_template = strdup(path)) == NULL) {
			vztt_logger(0, errno, "Cannot alloc memory");
			rc = VZT_CANT_ALLOC_MEM;
			goto cleanup_3;
		}

		snprintf(path, sizeof(path), "%s/templates", ploop_dir);
		if (symlink(path, ve_private_template))
			vztt_logger(0, errno, "Failed to create templates symlink");

		/*save VERSION link in private dir*/
		if ((rc = vefs_save_ver(ve_private, VZT_VE_LAYOUT3, veformat)))
			goto cleanup_3;
	} else {
		vztt_logger(1, 0, "Unpacking %s", cachename);
		get_unpack_cmd(cmd, sizeof(cmd), cachename, ve_private, "");
		if ((rc = exec_cmd(cmd, (opts_vztt->flags & OPT_VZTT_QUIET))))
			goto cleanup_3;
	}

	/* set VZFS technologies set according veformat */
	if ((rc = pm_set_veformat(to, veformat)))
		goto cleanup_3;

	snprintf(cmd, sizeof(cmd), \
		VZCTL " --skiplock %s start %s --skip_ve_setup",
		opts_vztt->debug < 4 ? "--quiet" : "--verbose", ctid);
	vztt_logger(2, 0, "system(\"%s\")", cmd);
	if ((rc = system(cmd)) == -1) {
		vztt_logger(0, errno, "system(%s) error", cmd);
		rc = VZT_CANT_EXEC;
		goto cleanup_3;
	}
	if (WEXITSTATUS(rc) == VZCTL_E_NO_LICENSE) {
		vztt_logger(0, 0, "VZ license not loaded, or invalid class ID");
		rc = VZT_NO_LICENSE;
		goto cleanup_3;
	}
	else if (WEXITSTATUS(rc)) {
		vztt_logger(0, 0, "\"%s\" return %d", cmd, WEXITSTATUS(rc));
		rc = VZT_CMD_FAILED;
		goto cleanup_3;
	}

	progress(PROGRESS_CREATE_TEMP_CONTAINER, 100, opts_vztt->progress_fd);

	/* lock template area on read */
	if ((rc = tmpl_lock(&gc, tmpl->base,
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_4;

	/* Call pre-cache script in VE0 context. */
 	if ((rc = tmplset_run_ve_scripts(tmpl, ctid, ve_root, "pre-update", 0,
 		opts_vztt->progress_fd)))
		goto cleanup_5;

	/* Get installed vz packages list */
	if ((rc = pm_get_installed_vzpkg(to, ve_private, &installed)))
		goto cleanup_5;

	/* Install new packages (if os template was updated) */
	for (p = packages.tqh_first; p != NULL; p = p->e.tqe_next) {
		if (pm_find_in_list(to, &installed, p->s) != 0) {
			if ((rc = string_list_add(&args, p->s))) {
				goto cleanup_5;
			}
		}
	}

	if (string_list_size(&args))
		if ((rc = to->pm_action(to, VZPKG_INSTALL, &args)))
			goto cleanup_5;

	/* Update all packages into VE (ts_args is empty) */
	string_list_clean(&args);
	if ((rc = to->pm_action(to, VZPKG_UPDATE, &args)))
		goto cleanup_5;

	if ((rc = to->pm_get_install_pkg(to, &installed)))
		goto cleanup_5;

	/* Call post-cache script in VE0 context. */
 	if ((rc = tmplset_run_ve_scripts(tmpl, ctid, ve_root, "post-update", 0,
 		opts_vztt->progress_fd)))
		goto cleanup_5;
	tmpl_unlock(lockdata, opts_vztt->flags);

	snprintf(cmd, sizeof(cmd), VZCTL " --skiplock --quiet stop %s --fast",\
			ctid);
	if ((rc = exec_cmd(cmd, (opts_vztt->flags & OPT_VZTT_QUIET))))
		goto cleanup_3;

	tmplset_update_privdir(tmpl, ve_private);

	if (ploop_dir)
		/* resize - ignore exit code */
		resize_ploop(ploop_dir, opts_vztt, 0);

	if ((rc = save_vzpackages(ve_private, &installed)))
		goto cleanup_3;

	/* do not rewrote tarball for test mode */
	if (opts_vztt->flags & OPT_VZTT_TEST)
		goto cleanup_3;

        /* Create tarball */
	snprintf(path, sizeof(path), "%s-old", cachename);
	if (access(cachename, F_OK) == 0) {
		move_file(path, cachename);
		backup = 1;
	} else {
		backup = 0;
	}

	/* Check for another archiver type and remove it too */
	if (tmpl_get_cache_tar(&gc, path, sizeof(path), gc.template_dir,
		tmpl->os->name) == 0)
		unlink(path);

	if (ploop_dir)
	{
		/*pack ploop device*/
		rc = pack_ploop(ploop_dir, cachename, opts_vztt);
	}
	else {
		if (chdir(ve_private) == -1) {
			vztt_logger(0, errno, "chdir(%s) failed", ve_private);
			if (backup)
				move_file(cachename, path);
			goto cleanup_3;
		}
		get_pack_cmd(cmd, sizeof(cmd), cachename, ".", " --numeric-owner");
		rc = exec_cmd(cmd, (opts_vztt->flags & OPT_VZTT_QUIET));
		if (pwd)
			if (chdir(pwd) == -1)
				vztt_logger(0, errno, "chdir(%s) failed", pwd);
	}

	if (rc) {
		unlink(cachename);
		/* restore old cache if failed */
		if (backup)
			move_file(cachename, path);
		goto cleanup_3;
	} else {
		if (backup)
			unlink(path);
	}

	goto cleanup_2;

cleanup_5:
	tmpl_unlock(lockdata, opts_vztt->flags);
cleanup_4:
	snprintf(cmd, sizeof(cmd), VZCTL " --skiplock --quiet stop %s --fast",\
			ctid);
	if (system(cmd) == -1)
		vztt_logger(0, errno, "system(%s) failed", cmd);
cleanup_3:
	if (ploop_dir)
		umount_ploop(ploop_dir, opts_vztt);
cleanup_2:
	if(ve_config)
		unlink(ve_config);
	pm_clean(to);
  cleanup_1_1:
	unlock_ve(ctid, velockdata, opts_vztt->flags);
cleanup_1:
	sigaction(SIGINT, &act_int, NULL);
cleanup_0:
	tmplset_clean(tmpl);

	VZTT_FREE_STR(ve_root);
	VZTT_FREE_STR(ve_private);
	VZTT_FREE_STR(ve_private_template);
	VZTT_FREE_STR(ploop_dir);

	if (ve_config)
		free((void *)ve_config);

	VZTT_FREE_STR(cachename);
	package_list_clean(&installed);
	string_list_clean(&packages);
	string_list_clean(&args);
	global_config_clean(&gc);
	vztt_config_clean(&tc);

	progress(PROGRESS_UPDATE_CACHE, 100, opts_vztt->progress_fd);

	return rc;
}

/* create os template cache */
int vztt_create_cache(
	char *ostemplate,
	struct options *opts,
	int skip_existed)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_create_cache(ostemplate, opts_vztt, skip_existed);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_create_cache(
	char *ostemplate,
	struct options_vztt *opts_vztt,
	int skip_existed)
{
	int rc;

	vztt_logger(1, 0,
		"Creating OS template cache for %s template", ostemplate);
	if ((rc = create_cache(ostemplate,
		skip_existed ? OPT_CACHE_SKIP_EXISTED : OPT_CACHE_FAIL_EXISTED,
		opts_vztt)))
		return rc;
	vztt_logger(1, 0, "OS template %s cache was created", ostemplate);
	return 0;
}

int vztt_update_cache(
	char *ostemplate,
	struct options *opts)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_update_cache(ostemplate, opts_vztt);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_update_cache(
	char *ostemplate,
	struct options_vztt *opts_vztt)
{
	int rc;

	vztt_logger(1, 0,
		"Update OS template cache for %s template", ostemplate);
	if ((opts_vztt->flags & OPT_VZTT_UPDATE_CACHE)
	    || (opts_vztt->flags & OPT_VZTT_TEST))
		rc = update_cache(ostemplate, opts_vztt);
	else
		rc = create_cache(ostemplate, OPT_CACHE_RECREATE, opts_vztt);
	if (rc)
		return rc;
	vztt_logger(1, 0, "OS template %s cache was updated", ostemplate);
	return 0;
}





/* remove cache file */
int vztt_remove_cache(
	char *ostemplate,
	struct options *opts)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_remove_cache(ostemplate, opts_vztt);
	vztt_options_free(opts_vztt);
	return rc;
}


int lock_and_remove_cache(const char *path, void *data)
{
	void *lockdata;
	struct callback_data *cdata = (struct callback_data *)data;
	int rc = 0;

	vztt_logger(1, 0, "Cache file name: %s", path);
	if (access(path, F_OK)) {
		vztt_logger(1, 0, "Cache file %s is not found", path);
		return -1;
	}

	/* it's enough for test mode */
	if (cdata->opts_vztt->flags & OPT_VZTT_TEST)
		return 0;

	/* lock template area on read */
	if (tmpl_lock(cdata->gc, cdata->tmpl->base,
			LOCK_READ, cdata->opts_vztt->flags, &lockdata)) {
		rc = -1;
		goto cleanup;
	}

	//printf ("remove %s\n", path);
	if (unlink(path) < 0) {
		vztt_logger(0, errno, "Can't unlink %s", path);
		rc = VZT_CANT_REMOVE;
		goto cleanup;
	}

cleanup:
	tmpl_unlock(lockdata, cdata->opts_vztt->flags);

	return rc;
}

/* remove cache file */
int vztt2_remove_cache(
	char *ostemplate,
	struct options_vztt *opts_vztt)
{
	int rc = 0;

	struct global_config gc;
	struct tmpl_set *tmpl;
	struct callback_data cdata;

	progress(PROGRESS_REMOVE_CACHE, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);

	vztt_logger(1, 0, "Removing OS template cache for %s template", ostemplate);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* init only base and os templates */
	if ((rc = tmplset_init(gc.template_dir, ostemplate, NULL, 0, &tmpl,
		opts_vztt->flags & ~OPT_VZTT_USE_VZUP2DATE)))
		goto cleanup;

	cdata.gc = &gc;
	cdata.tmpl = tmpl;
	cdata.opts_vztt = opts_vztt;

	rc = tmpl_callback_cache_tar(&gc, gc.template_dir, ostemplate,
		lock_and_remove_cache, &cdata);

	tmplset_clean(tmpl);
cleanup:
	global_config_clean(&gc);

	progress(PROGRESS_REMOVE_CACHE, 100, opts_vztt->progress_fd);

	return rc;
}
