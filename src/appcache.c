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
#include <time.h>

#include <vzctl/libvzctl.h>
#include <vzctl/vzerror.h>

#include "vztt.h"
#include "util.h"
#include "config.h"
#include "tmplset.h"
#include "lock.h"
#include "ploop.h"
#include "appcache.h"
#include "cache.h"
#include "md5.h"
#include "progress_messages.h"

#define APP_CACHE_SUFFIX "_app_"
#define APP_CACHE_LIST_SUFFIX ".list"

static int check_appcache_options(
	struct options_vztt *opts_vztt,
	struct global_config *gc,
	int load,
	struct tmpl_set **tmpl,
	struct string_list *apptemplates,
	struct string_list *unsupported_apptemplates,
	char **os_app_name,
	char **temp_list,
	char **config_path)
{
	int rc = 0, fd = 0, len = 0, j;
	char *ostemplate = NULL;
	char sample[PATH_MAX+1];
	char path[PATH_MAX+1];
	char *str, *token, *tempdir = 0;
	struct app_tmpl_list_el *a;
	struct string_list given_apptemplates;
	struct string_list sorted_apptemplates;
	struct string_list_el *p;
	unsigned char bin_buffer[16];
	FILE *fc = 0;
	char *saveptr = 0;

	string_list_init(&sorted_apptemplates);
	string_list_init(&given_apptemplates);

	if (opts_vztt->config)
	{
		/* Check for vzctl-like config name */
		vzctl2_get_config_full_fname(opts_vztt->config, sample,
			PATH_MAX);

		if (access(sample, F_OK))
			snprintf(sample, PATH_MAX, "%s", opts_vztt->config);
		else
			vzctl2_get_config_fname(opts_vztt->config, sample,
				PATH_MAX);
	}
	else
		snprintf(sample, PATH_MAX, CACHE_VE_CONF);

	snprintf(path, PATH_MAX, ENV_CONF_DIR "%s", sample);

	if (access(path, F_OK))
	{
		vztt_logger(1, 0, "%s sample config not found", sample);
		rc = VZT_TCACHE_CONF_NFOUND;
		goto cleanup;
	}
	else
	{
		(*config_path) = strndup(path, PATH_MAX);
		if (!*config_path)
		{
			vztt_logger(1, errno, "Cannot alloc memory");
			rc = VZT_CANT_ALLOC_MEM;
			goto cleanup;
		}
	}

	/* Check for Golden Image disabled in sample config,
	   override the global config value only if it was enabled */
	if (gc->golden_image == 1)
		if ((rc = ve_file_config_golden_image_read(sample, gc)))
			goto cleanup;

	/* Get ostemplate name */
	if ((rc = ve_file_config_ostemplate_read(sample, &ostemplate)))
		goto cleanup;

	/* Get app template list */
	if ((rc = ve_file_config_templates_read(sample, &given_apptemplates)))
		goto cleanup;

	if (opts_vztt->app_ostemplate)
	{
		VZTT_FREE_STR(ostemplate);
		ostemplate = strdup(opts_vztt->app_ostemplate);
	}

	if (!ostemplate)
	{
		vztt_logger(1, 0, "ostemplate name was not given");
		/* We don't want to return 0 in the vzpkg info appcache case */
		rc = VZT_TMPL_BROKEN;
		goto cleanup;
	}

	if (load)
	{
		if ((rc = tmplset_load(gc->template_dir, ostemplate, NULL,
			TMPLSET_LOAD_OS_LIST|TMPLSET_LOAD_APP_LIST, tmpl,
			opts_vztt->flags)))
				goto cleanup;
	}
	else
	{
		if ((rc = tmplset_init(gc->template_dir, ostemplate, NULL, 0, tmpl,
			opts_vztt->flags & ~OPT_VZTT_USE_VZUP2DATE)))
				goto cleanup;
	}

	/* Parse cmdline app templates if given */
	if (opts_vztt->app_apptemplate)
	{
		if (!string_list_empty(&given_apptemplates))
			string_list_clean(&given_apptemplates);

		for (str = opts_vztt->app_apptemplate; ;str = NULL)
		{
			if ((token = strtok_r(str, ",", &saveptr)) == NULL)
				break;
			if ((rc = string_list_add(&given_apptemplates, token)))
				goto cleanup;
		}
	}

	/* Check for Golden Image feature enabled */
	if (gc->golden_image == 0)
		vztt_logger(1, 0, "Golden Image feature disabled in config");
	else if ((*tmpl)->os->golden_image == 0)
		vztt_logger(1, 0, "Golden Image feature disabled in OS template");

	/* Fill the structures for given apptemplate names */
	if (load)
	{
		/* First check that all given apptemplates available on HN */
		if ((rc = tmplset_check_for_install((*tmpl), &given_apptemplates,
			opts_vztt->flags)))
			goto cleanup;

		#pragma GCC diagnostic ignored "-Waddress"
		/* Check for templates, that does not support Golden Image */
		for (a = (*tmpl)->avail_apps.tqh_first; a != NULL;
			a = a->e.tqe_next) \
			string_list_for_each(&given_apptemplates, p)
		{
			if (strncmp(p->s, a->tmpl->name, PATH_MAX) == 0)
			{
				if (gc->golden_image == 0 ||
					(*tmpl)->os->golden_image == 0)
				{
					/* Be quiet in case of feature disabled
					   in config */
					string_list_add(unsupported_apptemplates, p->s);
				}
				else if (a->tmpl->golden_image == 0)
				{
					vztt_logger(1, 0, "%s application template " \
					"does not support the Golden Image feature, " \
					"skipping...",
					a->tmpl->name);
					string_list_add(unsupported_apptemplates, p->s);
				}
				else
				{
					string_list_add(apptemplates, p->s);
				}
				string_list_remove(&given_apptemplates, p);
				break;
			}
		}
	}
	else
	{
		string_list_copy(apptemplates, &given_apptemplates);
	}

	/* Allocate os_app_name and temp_list */
	(*os_app_name) = malloc(PATH_MAX+1);
	(*temp_list) = malloc(PATH_MAX+1);
	if (!*os_app_name || !*temp_list)
	{
		vztt_logger(1, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup;
	}

	/* Check that at least one app template was given */
	if (string_list_empty(apptemplates))
	{
		vztt_logger(1, 0, "No apptemplate(s) with Golden Image " \
			"feature support was given");
		snprintf(*os_app_name, PATH_MAX, "%s", (*tmpl)->os->name);
		VZTT_FREE_STR(*config_path);
		return VZT_BAD_PARAM;
	}

	/* Get cache name appendix */
	string_list_copy(&sorted_apptemplates, apptemplates);
	string_list_sort(&sorted_apptemplates);

	/* Find the temp directory for app templates list */
	if ((rc = find_tmp_dir(&tempdir)))
		goto cleanup;

	snprintf(path, PATH_MAX, "%s/app_templates_listXXXXXX", tempdir);

	if ((fd = mkstemp(path)) == -1)
	{
		vztt_logger(1, errno, "Can't create temporary file in %s", tempdir);
		goto cleanup;
	}

	string_list_for_each(&sorted_apptemplates, p)
	{
		if (write(fd, "     ", 5) == -1 || write(fd, p->s, strlen(p->s)) == -1 ||
				write(fd, "\n", 1) == -1)
		{
			rc = VZT_CANT_WRITE;
			vztt_logger(1, errno, "Can't write create temporary file in %s", tempdir);
			goto cleanup;
		}
	}

	close(fd);

	if ((fc = fopen(path, "rb")) == NULL)
	{
		vztt_logger(0, errno, "fopen(%s) failed", path);
		rc = VZT_CANT_OPEN;
		goto cleanup;
	}

	if(MD5Stream(fc, bin_buffer))
	{
		vztt_logger(0, errno, "Can not calculate md5sum for %s", path);
		rc = VZT_CANT_CALC_MD5SUM;
		goto cleanup;
	}

	snprintf(*os_app_name, PATH_MAX, "%s" APP_CACHE_SUFFIX, (*tmpl)->os->name);
	len = strlen(*os_app_name);

	for(j = 0; j < sizeof(bin_buffer); ++j)
		snprintf(*os_app_name + len + j * 2, 3, "%02x", bin_buffer[j]);

	strncpy(*temp_list, path, PATH_MAX);

cleanup:
	if (fd > 0)
		close(fd);
	if (fc)
		fclose(fc);
	VZTT_FREE_STR(tempdir);
	VZTT_FREE_STR(ostemplate);
	if (rc)
	{
		string_list_clean(apptemplates);
		string_list_clean(unsupported_apptemplates);
		VZTT_FREE_STR(*os_app_name);
		VZTT_FREE_STR(*temp_list);
		VZTT_FREE_STR(*config_path);
	}
	string_list_clean(&sorted_apptemplates);
	string_list_clean(&given_apptemplates);
	return rc;
}

int install_templates(
	struct string_list *apptemplates,
	struct tmpl_set *tmpl,
	struct options_vztt *opts_vztt,
	struct Transaction *to,
	char *ve_private,
	char *ve_root,
	const char *ctid)
{
	int rc = 0;
	struct string_list args;
	struct string_list environment;
	struct package_list pkg_added;
	struct package_list pkg_removed;
	struct package_list pkg_existed;

	string_list_init(&args);
	string_list_init(&environment);
	package_list_init(&pkg_existed);
	package_list_init(&pkg_added);
	package_list_init(&pkg_removed);

	if ((rc = tmplset_get_marked_pkgs(tmpl, &args)))
		goto cleanup;

	/* Get installed vz packages list */
	if ((rc = pm_get_installed_vzpkg(to, ve_private, &pkg_existed)))
		goto cleanup;

	/* Setup the environmant */
	string_list_add(&environment, APP_CACHE_ENVIRONMENT);

	/* Call pre-install HN script */
	if (!(opts_vztt->flags & OPT_VZTT_TEST)) {
	 	if ((rc = tmplset_run_ve0_scripts(tmpl, ve_root, \
				ctid, "pre-install-hn", &environment,
				opts_vztt->progress_fd)))
			goto cleanup;
	}

	/* Call pre-install script */
	if (!(opts_vztt->flags & OPT_VZTT_TEST)) {
		if ((rc = tmplset_run_ve_scripts(tmpl, ctid, ve_root,
				"pre-install", &environment,
				opts_vztt->progress_fd)))
			goto cleanup;
	}

	if ((rc = pm_modify(to, VZPKG_INSTALL, &args, &pkg_added,
			&pkg_removed)))
		goto cleanup;

	/* Call post-install HN script */
	if (!(opts_vztt->flags & OPT_VZTT_TEST)) {
	 	if ((rc = tmplset_run_ve0_scripts(tmpl, ve_root, \
				ctid, "post-install-hn", &environment,
				opts_vztt->progress_fd)))
			goto cleanup;
	}

	/* Call post-install script */
	if (!(opts_vztt->flags & OPT_VZTT_TEST)) {
		if ((rc = tmplset_run_ve_scripts(tmpl, ctid, ve_root,
				"post-install", &environment,
				opts_vztt->progress_fd)))
		goto cleanup;
	}

	if ((rc = merge_pkg_lists(&pkg_added, &pkg_removed,
			&pkg_existed)))
		goto cleanup;

	/* and save it */
	if ((rc = save_vzpackages(ve_private, &pkg_existed)))
		goto cleanup;

cleanup:
	string_list_clean(&args);
	string_list_clean(&environment);
	package_list_clean(&pkg_existed);
	package_list_clean(&pkg_added);
	package_list_clean(&pkg_removed);
	return rc;
}

int vztt2_create_appcache(struct options_vztt *opts_vztt, int recreate)
{
	int rc, backup;
	char path[PATH_MAX+1];
	char base_cachename[PATH_MAX+1];
	char cmd[2*PATH_MAX+1];
	char *os_app_name = NULL;
	char *temp_list = NULL;
	char *config_path = NULL;
	char *cachename = NULL;
	char *ploop_dir = NULL;
	struct string_list apptemplates;
	struct string_list unsupported_apptemplates;
	struct string_list_el *p;
	struct global_config gc;
	struct vztt_config tc;
	struct tmpl_set *tmpl = NULL;
	struct sigaction act;
	struct sigaction act_int;

	struct Transaction *to;
	ctid_t ctid;
	unsigned veformat;
	void *lockdata, *velockdata;
	char *ve_config = NULL;
	char *ve_root = NULL;
	char *ve_private = NULL;
	char *ve_private_template = NULL;
	char *pwd = NULL;
	FILE *fp;

	progress(PROGRESS_CREATE_APPCACHE, 0, opts_vztt->progress_fd);

	if (getcwd(path, sizeof(path)))
		pwd = strdup(path);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* Check for vefstype all */
	if (gc.veformat == 0)
	{
		vztt_logger(0, 0, "Unsupported file system (vefstype): all");
		rc = VZT_BAD_PARAM;
		goto cleanup_0;
	}

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		goto cleanup_0;

	/* Get parameters */
	string_list_init(&apptemplates);
	string_list_init(&unsupported_apptemplates);
	if ((rc = check_appcache_options(opts_vztt, &gc, 1,
			&tmpl, &apptemplates, &unsupported_apptemplates,
			&os_app_name, &temp_list, &config_path)))
		goto cleanup_1;

	/* Check for pkg operations allowed */
	if (tmpl->base->no_pkgs_actions || tmpl->os->no_pkgs_actions) {
		vztt_logger(0, 0, "The OS template this Container is based " \
			"on does not support operations with packages.");
		rc = VZT_TMPL_PKGS_OPS_NOT_ALLOWED;
		goto cleanup_1;
	}

	/* file name for appcache */
	tmpl_get_cache_tar_name(path, sizeof(path), tc.archive,
				get_cache_type(&gc, opts_vztt->image_format), opts_vztt->vefstype, gc.template_dir, os_app_name);
	if ((cachename = strdup(path)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup_1;
	}

	/* Check for current cache */
	if ((access(cachename, F_OK) == 0) &&
		!recreate)
	{
		vztt_logger(1, 0, "Cache for OS template %s with"\
				" application template(s):",
				tmpl->os->name);
		string_list_for_each(&apptemplates, p)
			vztt_logger(1, 0, "     %s", p->s);
		vztt_logger(1, 0, "already exist");
		goto cleanup_2;
	}

	/* Skip create just convert image if old cache exist */
	if (get_cache_type(&gc, opts_vztt->image_format) & VZT_CACHE_TYPE_PLOOP_V2) {
		char old[PATH_MAX+1];
		tmpl_get_cache_tar_name(old, sizeof(path), tc.archive,
				VZT_CACHE_TYPE_SIMFS | VZT_CACHE_TYPE_PLOOP,
				opts_vztt->vefstype,
				gc.template_dir, os_app_name);

		if (access(old, F_OK) == 0) {
			rc = convert_ploop(old, cachename, opts_vztt);
			goto cleanup_2;
		}
	}

	/* if base OS cache file does not exist - run create_cache */
	tmpl_get_cache_tar_name(base_cachename, sizeof(base_cachename),
		tc.archive, get_cache_type(&gc, opts_vztt->image_format), opts_vztt->vefstype,
		gc.template_dir,
		tmpl->os->name);
	if (access(base_cachename, F_OK) != 0) {

		/* Recreate cache here for the case of old-format ploop cache */
		if ((rc = vztt2_create_cache(tmpl->os->name, opts_vztt, OPT_CACHE_FAIL_EXISTED)))
			goto cleanup_2;

		if (access(base_cachename, F_OK) != 0)
		{
			vztt_logger(1, 0, "Cache for OS template %s does not exist",
					tmpl->os->name);
		}
	}

	progress(PROGRESS_CREATE_TEMP_CONTAINER, 0, opts_vztt->progress_fd);

	vztt_logger(1, 0,
		"Creating OS template cache for %s template with"\
		" application templates:", tmpl->os->name);
	string_list_for_each(&apptemplates, p)
		vztt_logger(1, 0, "     %s", p->s);

	/* Disable SIGINT */
	sigaction(SIGINT, NULL, &act_int);
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = SIG_IGN;
	sigaction(SIGINT, &act, NULL);

	if ((rc = lock_free_veid(opts_vztt->flags, ctid, &velockdata)))
		goto cleanup_2;

	/* copy cache sample config to ctid.conf */
	snprintf(path, PATH_MAX, ENV_CONF_DIR "%s.conf", ctid);
	if (!(ve_config = strdup(path)))
	{
		vztt_logger(1, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup_3;
	}

	if ((rc = copy_container_config_file(ve_config, config_path)))
		goto cleanup_3;

	if ((rc = tmplset_mark(tmpl, &apptemplates,
			TMPLSET_MARK_AVAIL_APP_LIST,
			NULL)))
		goto cleanup_3;

	/* create & init package manager wrapper */
	if ((rc = pm_init(ctid, &gc, &tc, tmpl, opts_vztt, &to)))
		goto cleanup_3;

	/* add global custom exclude */
	if ((rc = pm_add_exclude(to, tc.exclude)))
		goto cleanup_4;

	/* create temporary root/private directory */
	snprintf(path, sizeof(path), "%s/cache-root", to->tmpdir);
	if (!(ve_root = strdup(path)))
	{
		vztt_logger(1, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup_4;
	}
	if (mkdir(ve_root, 0755))
	{
		vztt_logger(1, errno, "mkdir(%s) error", ve_root);
		rc = VZT_CANT_CREATE;
		goto cleanup_4;
	}
	snprintf(path, sizeof(path), "%s/cache-private", to->tmpdir);
	if (!(ve_private = strdup(path)))
	{
		vztt_logger(1, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup_4;
	}
	if (mkdir(ve_private, 0700))
	{
		vztt_logger(1, errno, "mkdir(%s) error", ve_private);
		rc = VZT_CANT_CREATE;
		goto cleanup_4;
	}
	/* add ve_root & ve_private into ve config */
	if ((fp = fopen(ve_config, "a")) == NULL)
	{
		vztt_logger(1, errno, "fopen(%s) error", ve_config);
		rc = VZT_CANT_OPEN;
		goto cleanup_4;
	}

	fprintf(fp, "VE_PRIVATE=%s\n", ve_private);
	fprintf(fp, "VE_ROOT=%s\n", ve_root);
	fprintf(fp, "OSTEMPLATE=.%s\n", tmpl->os->name);
	fclose(fp);

	if ((rc = pm_set_root_dir(to, ve_root)))
		goto cleanup_4;

	/* get veformat from old cache */
	if ((veformat = vzctl2_get_veformat(ve_private)) == -1)
		veformat = VZ_T_VZFS0;

	/* Create .ve.layout symlink */
	if ((rc = create_ve_layout(gc.velayout, ve_private)))
		goto cleanup_4;

	if (gc.velayout == VZT_VE_LAYOUT5)
	{
		if ((rc = create_ploop_dir(ve_private, opts_vztt->image_format, &ploop_dir)))
			goto cleanup_4;

		/*disable quota*/
		if ((fp = fopen(ve_config, "a")) == NULL) {
			vztt_logger(0, errno, "fopen(%s) error", ve_config);
			rc = VZT_CANT_OPEN;
			goto cleanup_4;
		}
		fprintf(fp, "DISK_QUOTA=no\n");
		fclose(fp);

		vztt_logger(1, 0, "Unpacking ploop %s", base_cachename);
		get_unpack_cmd(cmd, sizeof(cmd), base_cachename, ploop_dir, "");
		if ((rc = exec_cmd(cmd, (opts_vztt->flags & OPT_VZTT_QUIET))))
			goto cleanup_4;

		/* Get the required the ploop size */
		struct ve_config vc;

		/* read info from config */
		ve_config_init(&vc);
		if ((rc = ve_config_read(ctid, &gc, &vc, 0)))
			goto cleanup_4;

		if ((rc = resize_ploop(ploop_dir, opts_vztt,
			vc.diskspace)))
		{
			ve_config_clean(&vc);
			vztt_logger(0, 0, "Cannot resize ploop device");
			goto cleanup_4;
		}

		/* clean config */
		ve_config_clean(&vc);

		/* mounting ploop */
		if ((rc = mount_ploop(ploop_dir, ve_root, opts_vztt))) {
			vztt_logger(0, 0, "Cannot mount ploop device");
			rc = VZT_CANT_ALLOC_MEM;
			goto cleanup_5;
		}

		/*create link 'templates' in private directory */
		snprintf(path, sizeof(path), "%s/templates", ve_private);
		if ((ve_private_template = strdup(path)) == NULL) {
			vztt_logger(0, errno, "Cannot alloc memory");
			rc = VZT_CANT_ALLOC_MEM;
			goto cleanup_5;
		}

		snprintf(path, sizeof(path), "%s/templates", ploop_dir);
		if (symlink(path, ve_private_template))
			vztt_logger(0, errno, "Failed to create templates symlink");

		/*save VERSION link in private dir*/
		if ((rc = vefs_save_ver(ve_private, VZT_VE_LAYOUT3, veformat)))
			goto cleanup_5;
	} else {
		vztt_logger(1, 0, "Unpacking %s", base_cachename);
		get_unpack_cmd(cmd, sizeof(cmd), base_cachename, ve_private, "");
		if ((rc = exec_cmd(cmd, (opts_vztt->flags & OPT_VZTT_QUIET))))
			goto cleanup_4;
	}

	/* set VZFS technologies set according veformat */
	if ((rc = pm_set_veformat(to, veformat)))
		goto cleanup_5;

	if ((rc = do_vzctl("start", ctid, -1,
		(opts_vztt->debug < 4 ? DO_VZCTL_QUIET : DO_VZCTL_NONE) |
		DO_VZCTL_WAIT | DO_VZCTL_LOGGER) < 0))
	{
		rc = VZT_CANT_EXEC;
		goto cleanup_5;
	}
	else if (rc == VZCTL_E_NO_LICENSE)
	{
		vztt_logger(1, 0, "VZ license not loaded, or invalid class ID");
		rc = VZT_NO_LICENSE;
		goto cleanup_5;
	}
	else if (rc == VZCTL_E_WAIT_FAILED)
	{
		/* In this case Container is still runned! */
		vztt_logger(1, 0, "Failed to setup CT start wait functionality");
		rc = VZT_CMD_FAILED;
		goto cleanup_6;
	}
	else if (rc)
	{
		vztt_logger(1, 0, "\"%s\" return %d", cmd, rc);
		rc = VZT_CMD_FAILED;
		goto cleanup_5;
	}

	progress(PROGRESS_CREATE_TEMP_CONTAINER, 100, opts_vztt->progress_fd);

	/* lock template area on read */
	if ((rc = tmpl_lock(&gc, tmpl->base,
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_6;

	/* Set environment */
	string_list_add(&tmpl->os->environment, APP_CACHE_ENVIRONMENT);

	/*
	  To install application template(s) into cache.
	*/
	if ((rc = install_templates(&apptemplates, tmpl, opts_vztt, to,
				ve_private, ve_root, ctid)))
			goto cleanup_7;

	tmpl_unlock(lockdata, opts_vztt->flags);

	if ((rc = do_vzctl("stop", ctid, 1, DO_VZCTL_QUIET | DO_VZCTL_FAST)))
		goto cleanup_5;

	tmplset_update_privdir(tmpl, ve_private);

	if (ploop_dir)
		/* resize - ignore exit code */
		resize_ploop(ploop_dir, opts_vztt, 0);

	/* do not rewrote tarball for test mode */
	if (opts_vztt->flags & OPT_VZTT_TEST)
		goto cleanup_4;

	progress(PROGRESS_PACK_CACHE, 0, opts_vztt->progress_fd);

	/* Create tarball */
	snprintf(path, sizeof(path), "%s-old", cachename);
	if (access(cachename, F_OK) == 0)
	{
		move_file(path, cachename);
		backup = 1;
	} else {
		backup = 0;
	}

	/* Check for another archiver type and remove it too */
	if (tmpl_get_cache_tar(&gc, path, sizeof(path), gc.template_dir,
		os_app_name) == 0)
		unlink(path);

	if (ploop_dir) {
		/*pack ploop device to archive*/
		rc = pack_ploop(ploop_dir, cachename, opts_vztt);
	} else {
		if (chdir(ve_private) == -1) {
			vztt_logger(0, errno, "chdir(%s) failed", ve_private);
			if (backup)
				move_file(cachename, path);
			goto cleanup_4;
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
		goto cleanup_4;
	}

	if (backup)
		unlink(path);

	/* Save the list */
	tmpl_get_clean_os_name(cachename);
	snprintf(path, PATH_MAX, "%s" APP_CACHE_LIST_SUFFIX, cachename);
	if ((rc = move_file(path, temp_list)))
		goto cleanup_4;

	vztt_logger(1, 0, "OS template %s cache with application template(s):",
			tmpl->os->name);

	string_list_for_each(&apptemplates, p)
		vztt_logger(1, 0, "     %s", p->s);

	vztt_logger(1, 0, "was created");

	progress(PROGRESS_PACK_CACHE, 100, opts_vztt->progress_fd);

	goto cleanup_4;

	/* Cleanup */
cleanup_7:
	tmpl_unlock(lockdata, opts_vztt->flags);

cleanup_6:
	if ((rc = do_vzctl("stop", ctid, 1, DO_VZCTL_QUIET | DO_VZCTL_FAST)))
		vztt_logger(1, 0, "Failed to stop Container: %s", ctid);

cleanup_5:
	if (ploop_dir)
		umount_ploop(ploop_dir, opts_vztt);

cleanup_4:
	pm_clean(to);
cleanup_3:
	unlock_ve(ctid, velockdata, opts_vztt->flags);

cleanup_2:

	sigaction(SIGINT, &act_int, NULL);

	if (tmpl)
		tmplset_clean(tmpl);
	VZTT_FREE_STR(ve_root)
	VZTT_FREE_STR(ve_private)
	VZTT_FREE_STR(ve_private_template)
	VZTT_FREE_STR(ploop_dir)
	if (ve_config)
	{
		unlink(ve_config);
		free(ve_config);
	}
	VZTT_FREE_STR(os_app_name)
	unlink(temp_list);
	VZTT_FREE_STR(temp_list)
	VZTT_FREE_STR(config_path)
	VZTT_FREE_STR(pwd)

cleanup_1:
	VZTT_FREE_STR(cachename)
	string_list_clean(&apptemplates);
	string_list_clean(&unsupported_apptemplates);
	vztt_config_clean(&tc);

cleanup_0:
	global_config_clean(&gc);

	progress(PROGRESS_CREATE_APPCACHE, 100, opts_vztt->progress_fd);

	return rc;
}

int vztt2_update_appcache(struct options_vztt *opts_vztt)
{
	int rc, backup;
	char path[PATH_MAX+1];
	char cmd[2*PATH_MAX+1];
	char *cachename = NULL;
	char *temp_list = NULL;
	char *config_path = NULL;
	char *os_app_name = NULL;
	struct string_list apptemplates;
	struct string_list unsupported_apptemplates;
	struct string_list_el *p;
	struct global_config gc;
	struct vztt_config tc;
	struct tmpl_set *tmpl = NULL;

	struct string_list args;
	struct string_list environment;
	struct string_list packages;
	struct sigaction act;
	struct sigaction act_int;
	struct Transaction *to;
	struct package_list installed;
	ctid_t ctid;
	unsigned veformat;
	void *lockdata, *velockdata;
	char *ve_config = NULL;
	char *ve_root = NULL;
	char *ve_private = NULL;
	char *ve_private_template = NULL;
	char *pwd = NULL;
	char *ploop_dir = NULL;
	FILE *fp;

	/* Check for --update-cache option */
	if (!(opts_vztt->flags & OPT_VZTT_UPDATE_CACHE))
	{
		return vztt2_create_appcache(opts_vztt, 1);
	}

	progress(PROGRESS_UPDATE_APPCACHE, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	string_list_init(&packages);
	string_list_init(&args);
	string_list_init(&environment);
	package_list_init(&installed);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* Check for vefstype all */
	if (gc.veformat == 0)
	{
		vztt_logger(0, 0, "Unsupported file system (vefstype): all");
		rc = VZT_BAD_PARAM;
		goto cleanup_0;
	}

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		goto cleanup_0;

	/* Get parameters */
	string_list_init(&apptemplates);
	string_list_init(&unsupported_apptemplates);
	if ((rc = check_appcache_options(opts_vztt, &gc, 1,
			&tmpl, &apptemplates, &unsupported_apptemplates,
			&os_app_name, &temp_list, &config_path)))
		goto cleanup_1;

	/* Check for pkg operations allowed */
	if (tmpl->base->no_pkgs_actions || tmpl->os->no_pkgs_actions) {
		vztt_logger(0, 0, "The OS template this Container is based " \
			"on does not support operations with packages.");
		rc = VZT_TMPL_PKGS_OPS_NOT_ALLOWED;
		goto cleanup_1;
	}

	tmpl_get_cache_tar_name(path, sizeof(path), tc.archive,
				get_cache_type(&gc, opts_vztt->image_format), opts_vztt->vefstype,
				gc.template_dir, os_app_name);

	/* Check for current cache */
	if (access(path, F_OK))
	{
		vztt_logger(1, 0, "Cache for OS template %s with"\
				" application template(s):",
				tmpl->os->name);
		string_list_for_each(&apptemplates, p)
			vztt_logger(1, 0, "     %s", p->s);
		vztt_logger(1, 0, "does not exists");
		rc = VZT_TMPL_NOT_CACHED;
		goto cleanup_1;
	}

	if ((cachename = strdup(path)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup_1;
	}

	vztt_logger(1, 0,
		"Updating OS template cache for %s template with application"\
				" template(s):", tmpl->os->name);
	string_list_for_each(&apptemplates, p)
		vztt_logger(1, 0, "     %s", p->s);

	if (getcwd(path, PATH_MAX))
		pwd = strdup(path);

	/* check & update metadata */
	if ((rc = update_metadata(tmpl->os->name , &gc, &tc, opts_vztt)))
		goto cleanup_2;

	/* Set environment */
	string_list_add(&tmpl->os->environment, APP_CACHE_ENVIRONMENT);

	progress(PROGRESS_CREATE_TEMP_CONTAINER, 0, opts_vztt->progress_fd);

	/* Prepare temporary Container */
	if ((rc = tmplset_mark(tmpl, &apptemplates,
			TMPLSET_MARK_OS|TMPLSET_MARK_USED_APP_LIST, NULL)))
		goto cleanup_2;
	tmplset_get_marked_pkgs(tmpl, &packages);

	/* Disable SIGINT */
	sigaction(SIGINT, NULL, &act_int);
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = SIG_IGN;
	sigaction(SIGINT, &act, NULL);

	if ((rc = lock_free_veid(opts_vztt->flags, ctid, &velockdata)))
		goto cleanup_2;

	/* copy cache sample config to ctid.conf */
	snprintf(path, PATH_MAX, ENV_CONF_DIR "%s.conf", ctid);
	if (!(ve_config = strdup(path)))
	{
		vztt_logger(1, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup_3;
	}

	if ((rc = copy_container_config_file(ve_config, config_path)))
		goto cleanup_3;

	/* create & init package manager wrapper */
	if ((rc = pm_init(ctid, &gc, &tc, tmpl, opts_vztt, &to)))
		goto cleanup_3;

	/* add global custom exclude */
	if ((rc = pm_add_exclude(to, tc.exclude)))
		goto cleanup_4;

	/* create temporary root/private directory */
	snprintf(path, sizeof(path), "%s/cache-root", to->tmpdir);
	if (!(ve_root = strdup(path)))
	{
		vztt_logger(1, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup_4;
	}
	if (mkdir(ve_root, 0755))
	{
		vztt_logger(1, errno, "mkdir(%s) error", ve_root);
		rc = VZT_CANT_CREATE;
		goto cleanup_4;
	}
	snprintf(path, sizeof(path), "%s/cache-private", to->tmpdir);
	if (!(ve_private = strdup(path)))
	{
		vztt_logger(1, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup_4;
	}
	if (mkdir(ve_private, 0700))
	{
		vztt_logger(1, errno, "mkdir(%s) error", ve_private);
		rc = VZT_CANT_CREATE;
		goto cleanup_4;
	}
	/* add ve_root & ve_private into ve config */
	if ((fp = fopen(ve_config, "a")) == NULL)
	{
		vztt_logger(1, errno, "fopen(%s) error", ve_config);
		rc = VZT_CANT_OPEN;
		goto cleanup_4;
	}

	fprintf(fp, "VE_PRIVATE=%s\n", ve_private);
	fprintf(fp, "VE_ROOT=%s\n", ve_root);
	fprintf(fp, "OSTEMPLATE=.%s\n", tmpl->os->name);
	fclose(fp);

	if ((rc = pm_set_root_dir(to, ve_root)))
		goto cleanup_4;

	/* get veformat from old cache */
	if ((veformat = vzctl2_get_veformat(ve_private)) == -1)
		veformat = VZ_T_VZFS0;

	/* Create .ve.layout symlink */
	if ((rc = create_ve_layout(gc.velayout, ve_private)))
		goto cleanup_4;

	if (gc.velayout == VZT_VE_LAYOUT5)
	{
		if ((rc = create_ploop_dir(ve_private, opts_vztt->image_format,  &ploop_dir)))
			goto cleanup_4;

		/*disable quota*/
		if ((fp = fopen(ve_config, "a")) == NULL) {
			vztt_logger(0, errno, "fopen(%s) error", ve_config);
			rc = VZT_CANT_OPEN;
			goto cleanup_4;
		}
		fprintf(fp, "DISK_QUOTA=no\n");
		fclose(fp);

		vztt_logger(1, 0, "Unpacking ploop %s", cachename);
		get_unpack_cmd(cmd, sizeof(cmd), cachename, ploop_dir, "");
		if ((rc = exec_cmd(cmd, (opts_vztt->flags & OPT_VZTT_QUIET))))
			goto cleanup_4;

		/* Get the required the ploop size */
		struct ve_config vc;

		/* read info from config */
		ve_config_init(&vc);
		if ((rc = ve_config_read(ctid, &gc, &vc, 0)))
			goto cleanup_4;

		if ((rc = resize_ploop(ploop_dir, opts_vztt,
			vc.diskspace)))
		{
			ve_config_clean(&vc);
			vztt_logger(0, 0, "Cannot resize ploop device");
			goto cleanup_4;
		}

		/* clean config */
		ve_config_clean(&vc);

		/*mounting ploop*/
		if ((rc = mount_ploop(ploop_dir, ve_root, opts_vztt))) {
			vztt_logger(0, 0, "Cannot mount ploop device");
			rc = VZT_CANT_ALLOC_MEM;
			goto cleanup_5;
		}

		/*create link 'templates' in private directory */
		snprintf(path, sizeof(path), "%s/templates", ve_private);
		if ((ve_private_template = strdup(path)) == NULL) {
			vztt_logger(0, errno, "Cannot alloc memory");
			rc = VZT_CANT_ALLOC_MEM;
			goto cleanup_5;
		}

		/* Unlink old path */
		if (unlink(ve_private_template))
			vztt_logger(0, errno, "Failed to remove templates symlink");

		snprintf(path, sizeof(path), "%s/templates", ploop_dir);
		if (symlink(path, ve_private_template))
			vztt_logger(0, errno, "Failed to create templates symlink");

		/*save VERSION link in private dir*/
		if ((rc = vefs_save_ver(ve_private, VZT_VE_LAYOUT3, veformat)))
			goto cleanup_5;
	} else {
		vztt_logger(1, 0, "Unpacking %s", cachename);
		get_unpack_cmd(cmd, sizeof(cmd), cachename, ve_private, "");
		if ((rc = exec_cmd(cmd, (opts_vztt->flags & OPT_VZTT_QUIET))))
			goto cleanup_4;
	}

	/* set VZFS technologies set according veformat */
	if ((rc = pm_set_veformat(to, veformat)))
		goto cleanup_5;

	if ((rc = do_vzctl("start", ctid, -1,
		(opts_vztt->debug < 4 ? DO_VZCTL_QUIET : DO_VZCTL_NONE) |
		DO_VZCTL_WAIT | DO_VZCTL_LOGGER) < 0))
	{
		rc = VZT_CANT_EXEC;
		goto cleanup_5;
	}
	else if (rc == VZCTL_E_NO_LICENSE)
	{
		vztt_logger(1, 0, "VZ license not loaded, or invalid class ID");
		rc = VZT_NO_LICENSE;
		goto cleanup_5;
	}
	else if (rc == VZCTL_E_WAIT_FAILED)
	{
		/* In this case Container is still runned! */
		vztt_logger(1, 0, "Failed to setup CT start wait functionality");
		rc = VZT_CMD_FAILED;
		goto cleanup_6;
	}
	else if (rc)
	{
		vztt_logger(1, 0, "\"%s\" return %d", cmd, rc);
		rc = VZT_CMD_FAILED;
		goto cleanup_5;
	}

	progress(PROGRESS_CREATE_TEMP_CONTAINER, 100, opts_vztt->progress_fd);

	/* lock template area on read */
	if ((rc = tmpl_lock(&gc, tmpl->base,
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_6;

	/* Setup the environmant */
	string_list_add(&environment, APP_CACHE_ENVIRONMENT);

	/* Call pre-cache script in VE0 context. */
	if ((rc = tmplset_run_ve_scripts(tmpl, ctid, ve_root, "pre-update",
		&environment, opts_vztt->progress_fd)))
		goto cleanup_7;

	/* Get installed vz packages list */
	if ((rc = pm_get_installed_vzpkg(to, ve_private, &installed)))
		goto cleanup_7;

	/* Install new packages */
	for (p = packages.tqh_first; p != NULL; p = p->e.tqe_next)
	{
		if (pm_find_in_list(to, &installed, p->s) != 0)
		{
			if ((rc = string_list_add(&args, p->s)))
				goto cleanup_7;
		}
	}
	if (string_list_size(&args))
		if ((rc = to->pm_action(to, VZPKG_INSTALL, &args)))
			goto cleanup_7;

	/* Update all packages into VE (ts_args is empty) */
	string_list_clean(&args);
	if ((rc = to->pm_action(to, VZPKG_UPDATE, &args)))
		goto cleanup_7;

	if ((rc = to->pm_get_install_pkg(to, &installed)))
		goto cleanup_7;

	/* Call post-cache script in VE0 context. */
	if ((rc = tmplset_run_ve_scripts(tmpl, ctid, ve_root, "post-update",
		&environment, opts_vztt->progress_fd)))
		goto cleanup_7;

	tmpl_unlock(lockdata, opts_vztt->flags);

	if ((rc = do_vzctl("stop", ctid, 1, DO_VZCTL_QUIET | DO_VZCTL_FAST)))
		goto cleanup_4;

	tmplset_update_privdir(tmpl, ve_private);

	if (ploop_dir)
		/* resize - ignore exit code */
		resize_ploop(ploop_dir, opts_vztt, 0);

	progress(PROGRESS_PACK_CACHE, 0, opts_vztt->progress_fd);

	if ((rc = save_vzpackages(ve_private, &installed)))
		goto cleanup_4;

	/* do not rewrote tarball for test mode */
	if (opts_vztt->flags & OPT_VZTT_TEST)
		goto cleanup_4;

	/* Create tarball */
	snprintf(path, sizeof(path), "%s-old", cachename);
	if (access(cachename, F_OK) == 0)
	{
		move_file(path, cachename);
		backup = 1;
	} else {
		backup = 0;
	}

	/* Check for another archiver type and remove it too */
	if (tmpl_get_cache_tar(&gc, path, sizeof(path), gc.template_dir,
		os_app_name) == 0)
		unlink(path);

	if (ploop_dir) {
		/*pack ploop device to archive*/
		rc = pack_ploop(ploop_dir, cachename, opts_vztt);
	} else {
		if (chdir(ve_private) == -1) {
			vztt_logger(0, errno, "chdir(%s) failed", ve_private);
			if (backup)
				move_file(cachename, path);
			goto cleanup_4;
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
		goto cleanup_4;
	}

	if (backup)
		unlink(path);

	vztt_logger(1, 0, "OS template %s cache with application template(s):",
		    tmpl->os->name);

	string_list_for_each(&apptemplates, p)
		vztt_logger(1, 0, "     %s", p->s);

	vztt_logger(1, 0, "was updated");

	progress(PROGRESS_PACK_CACHE, 100, opts_vztt->progress_fd);

	goto cleanup_4;

	/* Cleanup */
cleanup_7:
	tmpl_unlock(lockdata, opts_vztt->flags);
	string_list_clean(&environment);

cleanup_6:
	if ((rc = do_vzctl("stop", ctid, 1, DO_VZCTL_QUIET | DO_VZCTL_FAST)))
		vztt_logger(1, 0, "Failed to stop Container: %s", ctid);

cleanup_5:
	if (ploop_dir)
		umount_ploop(ploop_dir, opts_vztt);

cleanup_4:
	pm_clean(to);

cleanup_3:
	unlock_ve(ctid, velockdata, opts_vztt->flags);

cleanup_2:
	sigaction(SIGINT, &act_int, NULL);
	if (tmpl)
		tmplset_clean(tmpl);
	VZTT_FREE_STR(ve_root)
	VZTT_FREE_STR(ve_private)
	VZTT_FREE_STR(ve_private_template)
	VZTT_FREE_STR(ploop_dir)
	if (ve_config)
	{
		unlink(ve_config);
		free(ve_config);
	}

	package_list_clean(&installed);
	string_list_clean(&args);
	string_list_clean(&packages);

	VZTT_FREE_STR(pwd)
	VZTT_FREE_STR(cachename);
	unlink(temp_list);
	VZTT_FREE_STR(temp_list)
	VZTT_FREE_STR(config_path)

cleanup_1:
	string_list_clean(&apptemplates);
	string_list_clean(&unsupported_apptemplates);
	vztt_config_clean(&tc);

cleanup_0:
	global_config_clean(&gc);

	progress(PROGRESS_UPDATE_APPCACHE, 100, opts_vztt->progress_fd);

	return rc;
}

int vztt2_remove_appcache(struct options_vztt *opts_vztt)
{
	int rc = 0;
	char *os_app_name = NULL;
	char *temp_list = NULL;
	char *config_path = NULL;
	struct string_list apptemplates;
	struct string_list unsupported_apptemplates;
	struct string_list_el *p;
	struct global_config gc;
	struct tmpl_set *tmpl = NULL;
	struct callback_data cdata;
	char path[PATH_MAX+1];

	progress(PROGRESS_REMOVE_APPCACHE, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* Get parameters */
	string_list_init(&apptemplates);
	string_list_init(&unsupported_apptemplates);
	if ((rc = check_appcache_options(opts_vztt, &gc, 0,
			&tmpl, &apptemplates, &unsupported_apptemplates,
			&os_app_name, &temp_list, &config_path)))
		goto cleanup_0;

	vztt_logger(1, 0, "Removing OS template cache %s with application"\
			" template(s)", tmpl->os->name);
	string_list_for_each(&apptemplates, p)
		vztt_logger(1, 0, "     %s", p->s);

	cdata.gc = &gc;
	cdata.tmpl = tmpl;
	cdata.opts_vztt = opts_vztt;

	if ((rc = tmpl_callback_cache_tar(&gc, gc.template_dir, os_app_name,
		lock_and_remove_cache, &cdata)))
		goto cleanup_0;

	/* Remove the list if no any caches exist */
	if (tmpl_get_cache_tar(0, path, sizeof(path), gc.template_dir,
		os_app_name) == -1)
	{
		/* Remove the list */
		snprintf(path, PATH_MAX, "%s/cache/%s" APP_CACHE_LIST_SUFFIX,
			gc.template_dir, os_app_name);
		unlink(path);
	}

cleanup_0:
	VZTT_FREE_STR(os_app_name);
	unlink(temp_list);
	VZTT_FREE_STR(temp_list)
	VZTT_FREE_STR(config_path)
	string_list_clean(&apptemplates);
	string_list_clean(&unsupported_apptemplates);
	if (tmpl)
		tmplset_clean(tmpl);
	global_config_clean(&gc);

	progress(PROGRESS_REMOVE_APPCACHE, 100, opts_vztt->progress_fd);

	return rc;
}

int vztt2_list_appcache(struct options_vztt *opts_vztt)
{
	int rc = 0, errno;
	char cache_dir[PATH_MAX+1];
	char path[PATH_MAX+1];
	char ostemplate[PATH_MAX+1];
	char tarball_path[PATH_MAX+1];
	char os_app_name[PATH_MAX+1];
	char *md5_begin;
	struct global_config gc;
	DIR *dir;
	char dirent_buf[sizeof(struct dirent) + PATH_MAX + 1];
	struct dirent *de = (struct dirent *) dirent_buf;
	struct dirent *result;
	int retval;
	struct stat st;
	struct tm *lt;

	/* struct initialization: should be first block */
	global_config_init(&gc);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	snprintf(cache_dir, PATH_MAX, "%s/cache/",
			gc.template_dir);

	/* scan directory with caches */
	dir = opendir(cache_dir);
	if (!dir)
	{
		vztt_logger(1, errno, "opendir(\"%s\") error", cache_dir);
		rc = VZT_CANT_OPEN;
		goto cleanup_0;
	}

	while (1)
	{
		if ((retval = readdir_r(dir, de, &result)) != 0)
		{
			vztt_logger(1, retval, "readdir_r(\"%s\") error",
					cache_dir);
			rc = VZT_CANT_READ;
			break;
		}

		if (result == NULL)
			break;

		/* Is file? */
		snprintf(path, PATH_MAX, "%s/%s", cache_dir, de->d_name);

		if (lstat(path, &st))
		{
			vztt_logger(1, errno, "stat(\"%s\") error", path);
			rc = VZT_CANT_LSTAT;
			break;
		}

		if (!S_ISREG(st.st_mode))
			continue;

		/* Find the list file; check for suffixes */
		if (!((md5_begin = strstr(de->d_name, APP_CACHE_SUFFIX))) ||
			!strstr(de->d_name, APP_CACHE_LIST_SUFFIX))
			continue;

		/* Check that appropriate tarball exist */
		snprintf(os_app_name, strlen(de->d_name) -
				strlen(APP_CACHE_LIST_SUFFIX) + 1, "%s", de->d_name);

		if (tmpl_get_cache_tar(&gc, tarball_path, sizeof(tarball_path),
				gc.template_dir, os_app_name) != 0)
			continue;

		snprintf(ostemplate, strlen(de->d_name) -
				strlen(md5_begin) + 1, "%s", de->d_name);

		if (opts_vztt->flags & OPT_VZTT_QUIET)
		{
			printf("%s\n", ostemplate);
		}
		else
		{
			/* get timestamp from tarball mtime */
			if (lstat(tarball_path, &st))
			{
				vztt_logger(1, errno, "stat(\"%s\") error", path);
				rc = VZT_CANT_LSTAT;
				break;
			}
			lt = localtime(&st.st_mtime);
			printf("%-34s %04d-%02d-%02d %02d:%02d:%02d\n", ostemplate, \
				lt->tm_year+1900, lt->tm_mon+1, lt->tm_mday, \
				lt->tm_hour, lt->tm_min, lt->tm_sec);
		}

		copy_file_fd(1, "/dev/stdout", path);

		printf("\n");
	}

	closedir(dir);

cleanup_0:
	global_config_clean(&gc);

	return rc;
}

int info_appcache(struct options_vztt *opts_vztt)
{
	int rc = 0;
	char *os_app_name = NULL;
	char *temp_list = NULL;
	char *config_path = NULL;
	char path[PATH_MAX+1];
	struct string_list apptemplates;
	struct string_list unsupported_apptemplates;
	struct string_list_el *p;
	struct global_config gc;
	struct tmpl_set *tmpl = NULL;

	/* struct initialization: should be first block */
	global_config_init(&gc);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	string_list_init(&apptemplates);
	string_list_init(&unsupported_apptemplates);

	/* Get parameters */
	rc = check_appcache_options(opts_vztt, &gc, 1,
			&tmpl, &apptemplates, &unsupported_apptemplates,
			&os_app_name, &temp_list, &config_path);

	/* Ignore the empty apptemplate list case */
	if ((rc != 0 && rc != VZT_BAD_PARAM) || !os_app_name)
		goto cleanup;

	rc = 0;
	/* Try to find the cache with all archive types,
		but with special case for ploop v2: will
		ignore old ploop cache */
	if (gc.veformat && tmpl_get_cache_tar_by_type(path, sizeof(path), get_cache_type(&gc, opts_vztt->image_format), opts_vztt->vefstype,
				gc.template_dir, os_app_name) != 0) {
		rc = VZT_TMPL_NOT_CACHED;
	} else if (tmpl_get_cache_tar(&gc, path, sizeof(path), gc.template_dir,
				os_app_name) != 0)
		rc = VZT_TMPL_NOT_CACHED;

	if (rc == VZT_TMPL_NOT_CACHED)
	{
		vztt_logger(1, 0, "Cache %s is not found", os_app_name);
		goto cleanup;
	}

	/* Print base cache name */
	printf("%s\n", path);

	/* Print the unsupported app templates list if any */
	string_list_for_each(&unsupported_apptemplates, p)
		printf("%s\n", p->s);

cleanup:
	VZTT_FREE_STR(os_app_name);
	unlink(temp_list);
	VZTT_FREE_STR(temp_list)
	VZTT_FREE_STR(config_path)
	string_list_clean(&apptemplates);
	string_list_clean(&unsupported_apptemplates);
	if (tmpl)
		tmplset_clean(tmpl);
	global_config_clean(&gc);

	return rc;
}
