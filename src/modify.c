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
 * vztt install/update/remove for template & packages
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

#include "vztt.h"
#include "util.h"
#include "tmplset.h"
#include "config.h"
#include "lock.h"
#include "appcache.h"
#include "progress_messages.h"

/* install/update/remove packages set <packages> in VE <veid> */
static int cmd_modify(
	int cmd,
	const char *ctid,
	char *packages[],
	size_t size,
	struct package ***pkg_added,
	struct package ***pkg_removed,
	struct options_vztt *opts_vztt)
{
	int rc = 0;
	size_t i;
	struct sigaction act;
	struct sigaction act_int;
	void *lockdata, *velockdata;

	struct package_list existed;
	struct package_list removed;
	struct package_list added;

	struct string_list args;
	struct string_list apps;

	struct global_config gc;
	struct vztt_config tc;
	struct ve_config vc;

	struct tmpl_set *tmpl;
	struct Transaction *to;

	progress(PROGRESS_MODIFY, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	ve_config_init(&vc);
	package_list_init(&existed);
	package_list_init(&removed);
	package_list_init(&added);
	string_list_init(&args);
	string_list_init(&apps);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	if ((rc = check_n_load_ve_config(ctid,
			(opts_vztt->flags & OPT_VZTT_TEST) ?
			ENV_STATUS_MOUNTED : ENV_STATUS_RUNNING,
			&gc, &vc)))
		return rc;

	/* load corresponding os template in configs directory */
	if ((rc = tmplset_load(gc.template_dir, vc.ostemplate, &vc.templates, \
			TMPLSET_LOAD_APP_LIST, &tmpl, opts_vztt->flags)))
		return rc;

	/* Check for pkg operations allowed */
	if (tmpl->base->no_pkgs_actions || tmpl->os->no_pkgs_actions) {
		vztt_logger(0, 0, "The OS template this Container is based " \
			"on does not support operations with packages.");
		rc = VZT_TMPL_PKGS_OPS_NOT_ALLOWED;
		goto cleanup_0;
	}

	/* check & update metadata */
	if ((rc = update_metadata(vc.ostemplate, &gc, &tc, opts_vztt)))
		goto cleanup_0;

	/* this mark will use for get_urls only */
	if ((rc = tmplset_mark(tmpl, &vc.templates, \
			TMPLSET_MARK_OS|TMPLSET_MARK_USED_APP_LIST, NULL)))
		goto cleanup_0;

	/* create & init package manager wrapper */
	if ((rc = pm_init(ctid, &gc, &tc, tmpl, opts_vztt, &to)))
		goto cleanup_0;

	if ((rc = pm_set_root_dir(to, vc.ve_root)))
		goto cleanup_1;

	/* use only local (per-VE) veformat from VE private */
	if ((rc = pm_set_veformat(to, vc.veformat)))
		goto cleanup_1;

	/* add global custom exclude */
	if ((rc = pm_add_exclude(to, tc.exclude)))
		goto cleanup_1;
	/* add per_VE exclude */
	if ((rc = pm_add_exclude(to, vc.exclude)))
		goto cleanup_1;

	for (i = 0; i < size; i++)
		string_list_add(&args, packages[i]);

	/* lock VE */
	if ((rc = lock_ve(ctid, opts_vztt->flags, &velockdata)))
		goto cleanup_1;

	/* Get installed vz packages list */
	if ((rc = pm_get_installed_vzpkg(to, vc.ve_private, &existed)))
		goto cleanup_2;

	/* ignore SIGINT */
	sigaction(SIGINT, NULL, &act_int);
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = SIG_IGN;
	sigaction(SIGINT, &act, NULL);

	/* lock template area on read */
	if ((rc = tmpl_lock(&gc, tmpl->base, 
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_3;

	set_trusted(ctid, "1");

	switch(cmd) {
		case VZPKG_INSTALL:
			/* Install packages into VE */
			if ((rc = pm_modify(to, VZPKG_INSTALL, &args, \
					&added, &removed)))
				goto cleanup_4;
			break;
		case VZPKG_UPDATE:
			/* Call pre-update script */
			if (!(opts_vztt->flags & OPT_VZTT_TEST)) {
				if ((rc = tmplset_run_ve_scripts(tmpl, ctid, vc.ve_root, \
						"pre-update", 0, opts_vztt->progress_fd)))
					goto cleanup_4;
			}

			/* Install packages into VE */
			if (string_list_empty(&args))
				string_list_add(&args, "*");

			if ((rc = pm_modify(to, VZPKG_UPDATE, &args, \
					&added, &removed)))
				goto cleanup_4;

			/* Call post-update script */
			if (!(opts_vztt->flags & OPT_VZTT_TEST)) {
				if ((rc = tmplset_run_ve_scripts(tmpl, ctid, vc.ve_root, \
						"post-update", 0, opts_vztt->progress_fd)))
					goto cleanup_4;
			}
			break;
		case VZPKG_REMOVE: {
			struct package_list installed;

			progress(PROGRESS_REMOVE_PACKAGES, 0, opts_vztt->progress_fd);
			package_list_init(&installed);
			/* Get installed packages list */
			if ((rc = to->pm_get_install_pkg(to, &installed)))
				goto cleanup_4;

			/* Remove packages from VE */
			if ((rc = to->pm_remove_pkg(to, &args, \
					&installed, &removed)))
				goto cleanup_4;
			package_list_clean(&installed);
			progress(PROGRESS_REMOVE_PACKAGES, 100, opts_vztt->progress_fd);
			break;
		}
		case VZPKG_GROUPINSTALL:
			/* Install groups of packages into VE */
			if ((rc = pm_modify(to, VZPKG_GROUPINSTALL, &args, &added, &removed)))
				goto cleanup_4;
			break;
		case VZPKG_GROUPUPDATE:
			/* Update groups of packages in VE */
			if ((rc = pm_modify(to, VZPKG_GROUPUPDATE, &args, &added, &removed)))
				goto cleanup_4;
			break;
		case VZPKG_GROUPREMOVE:
			/* Remove groups of packages from VE */
			if ((rc = pm_modify(to, VZPKG_GROUPREMOVE, &args, &added, &removed)))
				goto cleanup_4;
			break;
	}
	set_trusted(ctid, "0");
	tmpl_unlock(lockdata, opts_vztt->flags);

	/* fill output packages arrays */
	if (pkg_added)
		if ((rc = package_list_to_array(&added, pkg_added)))
			goto cleanup_3;
	if (pkg_removed)
		if ((rc = package_list_to_array(&removed, pkg_removed)))
			goto cleanup_3;

	/* if it's test transaction - success */
	if (opts_vztt->flags & OPT_VZTT_TEST)
		goto cleanup_3;

	if ((rc = merge_pkg_lists(&added, &removed, &existed)))
		goto cleanup_3;

	/* and save it */
	if ((rc = save_vzpackages(vc.ve_private, &existed)))
		goto cleanup_3;

	/* if application template autodetection is off - do not change
	   CT app templates list during packages install/remove */
	if (tc.apptmpl_autodetect) {
		/* process templates */
		tmplset_check_apps(tmpl, to, vc.ve_private, &existed, &apps);

		/* save new app templates set in VE config */
		if ((rc = ve_config1_save(vc.config, &apps)))
			goto cleanup_3;
	}

	goto cleanup_3;
cleanup_4:
	set_trusted(ctid, "0");
	tmpl_unlock(lockdata, opts_vztt->flags);
cleanup_3:
	sigaction(SIGINT, &act_int, NULL);
cleanup_2:
	unlock_ve(ctid, velockdata, opts_vztt->flags);
cleanup_1:
	/* cleanup without exit */
	pm_clean(to);
cleanup_0:
	tmplset_clean(tmpl);

	package_list_clean(&existed);
	package_list_clean(&removed);
	package_list_clean(&added);
	string_list_clean(&args);
	string_list_clean(&apps);
	global_config_clean(&gc);
	vztt_config_clean(&tc);
	ve_config_clean(&vc);

	progress(PROGRESS_MODIFY, 100, opts_vztt->progress_fd);

	return rc;
}

/* install packages into VE */
int vztt_install(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options *opts,
	struct package ***pkg_added,
	struct package ***pkg_removed)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = cmd_modify(VZPKG_INSTALL, ctid, packages, size, \
		pkg_added, pkg_removed, opts_vztt);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_install(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_added,
	struct package ***pkg_removed)
{
	return cmd_modify(VZPKG_INSTALL, ctid, packages, size, \
		pkg_added, pkg_removed, opts_vztt);
}

/* update packages in VE */
int vztt_update(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options *opts,
	struct package ***pkg_updated,
	struct package ***pkg_removed)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = cmd_modify(VZPKG_UPDATE, ctid, packages, size, \
		pkg_updated, pkg_removed, opts_vztt);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_update(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_updated,
	struct package ***pkg_removed)
{

	return cmd_modify(VZPKG_UPDATE, ctid, packages, size, \
		pkg_updated, pkg_removed, opts_vztt);
}

/* remove packages from VE */
int vztt_remove(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options *opts,
	struct package ***pkg_removed)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_remove(ctid, packages, size, opts_vztt, pkg_removed);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_remove(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_removed)
{
	struct package **pkg_added;
	int rc;

	rc = cmd_modify(VZPKG_REMOVE, ctid, packages, size, \
		&pkg_added, pkg_removed, opts_vztt);

	return rc;
}

/* install groups of packages into VE (for rpm-based templates only) */
int vztt_install_group(
	const char *ctid,
	char *groups[],
	size_t size,
	struct options *opts,
	struct package ***pkg_added,
	struct package ***pkg_removed)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = cmd_modify(VZPKG_GROUPINSTALL, ctid, groups, size, pkg_added, \
			pkg_removed, opts_vztt);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_install_group(
	const char *ctid,
	char *groups[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_added,
	struct package ***pkg_removed)
{
	return cmd_modify(VZPKG_GROUPINSTALL, ctid, groups, size, pkg_added, \
			pkg_removed, opts_vztt);
}

/* update groups of packages in VE (for rpm-based templates only) */
int vztt_update_group(
	const char *ctid,
	char *groups[],
	size_t size,
	struct options *opts,
	struct package ***pkg_updated,
	struct package ***pkg_removed)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = cmd_modify(VZPKG_GROUPUPDATE, ctid, groups, size, pkg_updated, \
			pkg_removed, opts_vztt);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_update_group(
	const char *ctid,
	char *groups[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_updated,
	struct package ***pkg_removed)
{

	return cmd_modify(VZPKG_GROUPUPDATE, ctid, groups, size, pkg_updated, \
			pkg_removed, opts_vztt);
}

/* remove groups of packages from VE (for rpm-based templates only) */
int vztt_remove_group(
	const char *ctid,
	char *groups[],
	size_t size,
	struct options *opts,
	struct package ***pkg_removed)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_remove_group(ctid, groups, size, opts_vztt, pkg_removed);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_remove_group(
	const char *ctid,
	char *groups[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_removed)
{
	struct package **pkg_added;
	int rc;

	rc = cmd_modify(VZPKG_GROUPREMOVE, ctid, groups, size, &pkg_added, \
			pkg_removed, opts_vztt);

	return rc;
}

/* install templates <tlist> into VE <veid> */
int vztt_install_tmpl(
	const char *ctid,
	char *tlist[],
	size_t size,
	struct options *opts,
	struct package ***pkg_added,
	struct package ***pkg_removed)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_install_tmpl(ctid, tlist, size, opts_vztt, pkg_added, \
		pkg_removed);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_install_tmpl(
	const char *ctid,
	char *tlist[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_added,
	struct package ***pkg_removed)
{
	int rc = 0;
	size_t i;
	void *lockdata, *velockdata;
	struct sigaction act;
	struct sigaction act_int;

	struct package_list existed;
	struct package_list removed;
	struct package_list added;

	struct string_list args;
	struct string_list apps;
	struct string_list_el *p;

	struct ve_config vc;
	struct global_config gc;
	struct vztt_config tc;

	struct tmpl_set *tmpl;
	struct Transaction *to;
	char progress_stage[PATH_MAX];

	snprintf(progress_stage, sizeof(progress_stage),
		PROGRESS_INSTALL_APPTEMPLATES);
	for (i = 0; i < size; i++)
	{
		strncat(progress_stage, " ", sizeof(progress_stage) -
			strlen(progress_stage) - 1);
		strncat(progress_stage, tlist[i], sizeof(progress_stage) -
			strlen(progress_stage) - 1);
	}
	progress(progress_stage, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	ve_config_init(&vc);
	package_list_init(&existed);
	package_list_init(&removed);
	package_list_init(&added);
	string_list_init(&args);
	string_list_init(&apps);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	if ((rc = check_n_load_ve_config(ctid, ENV_STATUS_RUNNING, &gc, &vc)))
		return rc;

	/* load corresponding os template in configs directory */
	if ((rc = tmplset_load(gc.template_dir, vc.ostemplate, &vc.templates, \
			TMPLSET_LOAD_APP_LIST, &tmpl, opts_vztt->flags)))
		return rc;

	/* Check for pkg operations allowed */
	if (tmpl->base->no_pkgs_actions || tmpl->os->no_pkgs_actions) {
		vztt_logger(0, 0, "The OS template this Container is based " \
			"on does not support operations with packages.");
		rc = VZT_TMPL_PKGS_OPS_NOT_ALLOWED;
		goto cleanup_0;
	}

	/* check & update metadata */
	if ((rc = update_metadata(vc.ostemplate, &gc, &tc, opts_vztt)))
		goto cleanup_0;

	/*
	  To include in transaction repositories/mirrorlists for OS template,
	  installed app templates and target templates (#482856).
	  Important : _now_ pm_init() use for marked
	  templates repositories/mirrorlists only.
	*/
	string_list_add(&apps, vc.ostemplate);
	for (p = vc.templates.tqh_first; p != NULL; p = p->e.tqe_next)
		string_list_add(&apps, p->s);
	for (i = 0; i < size; i++)
		string_list_add(&apps, tlist[i]);
	if ((rc = tmplset_mark(tmpl, &apps,
			TMPLSET_MARK_OS |
			TMPLSET_MARK_USED_APP_LIST |
			TMPLSET_MARK_AVAIL_APP_LIST,
			NULL)))
		goto cleanup_0;
	/* create & init package manager wrapper */
	if ((rc = pm_init(ctid, &gc, &tc, tmpl, opts_vztt, &to)))
		goto cleanup_0;
	string_list_clean(&apps);
	tmplset_unmark_all(tmpl);


	/* now will work with target templates only */
	for (i = 0; i < size; i++)
		string_list_add(&apps, tlist[i]);
	if ((rc = tmplset_check_for_install(tmpl, &apps, opts_vztt->flags)))
		goto cleanup_0;

	if ((rc = tmplset_mark(tmpl, &apps, TMPLSET_MARK_AVAIL_APP_LIST, NULL)))
		goto cleanup_0;

	if ((rc = tmplset_get_marked_pkgs(tmpl, &args)))
		goto cleanup_0;
	string_list_clean(&apps);

	if ((rc = pm_set_root_dir(to, vc.ve_root)))
		goto cleanup_1;

	/* use only local (per-VE) veformat from VE private */
	if ((rc = pm_set_veformat(to, vc.veformat)))
		goto cleanup_1;

	/* add global custom exclude */
	if ((rc = pm_add_exclude(to, tc.exclude)))
		goto cleanup_1;
	/* add per_VE exclude */
	if ((rc = pm_add_exclude(to, vc.exclude)))
		goto cleanup_1;

	/* lock VE */
	if ((rc = lock_ve(ctid, opts_vztt->flags, &velockdata)))
		goto cleanup_1;

	/* Get installed vz packages list */
	if ((rc = pm_get_installed_vzpkg(to, vc.ve_private, &existed)))
		goto cleanup_2;

	/* ignore SIGINT */
	sigaction(SIGINT, NULL, &act_int);
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = SIG_IGN;
	sigaction(SIGINT, &act, NULL);

	/* lock template area on read */
	if ((rc = tmpl_lock(&gc, tmpl->base, 
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_3;

	/* Call pre-install HN script */
	if (!(opts_vztt->flags & OPT_VZTT_TEST)) {
	 	if ((rc = tmplset_run_ve0_scripts(tmpl, vc.ve_root, \
				ctid, "pre-install-hn", 0,
				opts_vztt->progress_fd)))
			goto cleanup_4;
	}

	/* Call pre-install script */
	if (!(opts_vztt->flags & OPT_VZTT_TEST)) {
	 	if ((rc = tmplset_run_ve_scripts(tmpl, ctid, vc.ve_root, \
				"pre-install", 0, opts_vztt->progress_fd)))
			goto cleanup_4;
	}

	/* Install packages into VE */
	if ((rc = pm_modify(to, VZPKG_INSTALL, &args, &added, &removed)))
		goto cleanup_4;

	/* Call post-install HN script */
	if (!(opts_vztt->flags & OPT_VZTT_TEST)) {
	 	if ((rc = tmplset_run_ve0_scripts(tmpl, vc.ve_root, \
				ctid, "post-install-hn", 0,
				opts_vztt->progress_fd)))
			goto cleanup_4;
	}

	/* Call post-install script */
	if (!(opts_vztt->flags & OPT_VZTT_TEST)) {
	 	if ((rc = tmplset_run_ve_scripts(tmpl, ctid, vc.ve_root, \
				"post-install", 0, opts_vztt->progress_fd)))
			goto cleanup_4;
	}
	tmpl_unlock(lockdata, opts_vztt->flags);

	/* fill output packages arrays */
	if (pkg_added)
		if ((rc = package_list_to_array(&added, pkg_added)))
			goto cleanup_3;
	if (pkg_removed)
		if ((rc = package_list_to_array(&removed, pkg_removed)))
			goto cleanup_3;

	/* if it's test transaction - success */
	if (opts_vztt->flags & OPT_VZTT_TEST)
		goto cleanup_3;

	if ((rc = merge_pkg_lists(&added, &removed, &existed)))
		goto cleanup_3;

	/* and save it */
	if ((rc = save_vzpackages(vc.ve_private, &existed)))
		goto cleanup_3;

	/* if application template autodetection is off,
	   to add directly installed templates only */
	if (tc.apptmpl_autodetect)
		tmplset_add_marked_and_check_apps(tmpl, to,
				vc.ve_private, &existed, &apps);
	else
		tmplset_add_marked_apps(tmpl, vc.ve_private, &apps);

	/* save new app templates set in VE config */
	if ((rc = ve_config1_save(vc.config, &apps)))
		goto cleanup_3;

	goto cleanup_3;
cleanup_4:
	tmpl_unlock(lockdata, opts_vztt->flags);
cleanup_3:
	sigaction(SIGINT, &act_int, NULL);
cleanup_2:
	unlock_ve(ctid, velockdata, opts_vztt->flags);
cleanup_1:
	pm_clean(to);
cleanup_0:
	tmplset_clean(tmpl);

	string_list_clean(&args);
	string_list_clean(&apps);
	package_list_clean(&existed);
	package_list_clean(&removed);
	package_list_clean(&added);
	global_config_clean(&gc);
	vztt_config_clean(&tc);
	ve_config_clean(&vc);

	progress(progress_stage, 100, opts_vztt->progress_fd);

	return rc;
}

/* update templates <tlist> in VE <veid> */
int vztt_update_tmpl(
	const char *ctid,
	char *tlist[],
	size_t size,
	struct options *opts,
	struct package ***pkg_added,
	struct package ***pkg_removed)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_update_tmpl(ctid, tlist, size, opts_vztt, pkg_added, \
		pkg_removed);
	vztt_options_free(opts_vztt);
	return rc;
}


int vztt2_update_tmpl(
	const char *ctid,
	char *tlist[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_added,
	struct package ***pkg_removed)
{
	int rc = 0;
	void *lockdata, *velockdata;
	struct sigaction act;
	struct sigaction act_int;

	struct package_list existed;
	struct package_list removed;
	struct package_list added;

	struct string_list args;
	struct string_list apps;

	struct ve_config vc;
	struct global_config gc;
	struct vztt_config tc;

	struct tmpl_set *tmpl;
	struct Transaction *to;
	size_t i;
	char progress_stage[PATH_MAX];

	snprintf(progress_stage, sizeof(progress_stage),
		PROGRESS_UPDATE_APPTEMPLATES);
	for (i = 0; i < size; i++)
	{
		strncat(progress_stage, " ", sizeof(progress_stage) -
			strlen(progress_stage) - 1);
		strncat(progress_stage, tlist[i], sizeof(progress_stage) -
			strlen(progress_stage) - 1);
	}
	progress(progress_stage, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	ve_config_init(&vc);
	package_list_init(&existed);
	package_list_init(&removed);
	package_list_init(&added);
	string_list_init(&args);
	string_list_init(&apps);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	if ((rc = check_n_load_ve_config(ctid, ENV_STATUS_RUNNING, &gc, &vc)))
		return rc;

	/* load corresponding os template in configs directory */
	if ((rc = tmplset_load(gc.template_dir, vc.ostemplate, &vc.templates, \
			TMPLSET_LOAD_APP_LIST, &tmpl, opts_vztt->flags)))
		return rc;

	/* Check for pkg operations allowed */
	if (tmpl->base->no_pkgs_actions || tmpl->os->no_pkgs_actions) {
		vztt_logger(0, 0, "The OS template this Container is based " \
			"on does not support operations with packages.");
		rc = VZT_TMPL_PKGS_OPS_NOT_ALLOWED;
		goto cleanup_0;
	}

	/* check & update metadata */
	if ((rc = update_metadata(vc.ostemplate, &gc, &tc, opts_vztt)))
		goto cleanup_0;

	string_list_init(&apps);
	if (size > 0) {
		for (i = 0; i < size; i++)
			string_list_add(&apps, tlist[i]);
		if ((rc = tmplset_check_for_update(tmpl, &apps,
				opts_vztt->flags)))
			goto cleanup_0;
	} else {
		/* template update without parameters - 
		to update all templates */
		struct string_list_el *p;
		string_list_add(&apps, vc.ostemplate);
		for (p = vc.templates.tqh_first; p != NULL; p = p->e.tqe_next)
			string_list_add(&apps, p->s);
	}
	if ((rc = tmplset_mark(tmpl, &apps, \
			TMPLSET_MARK_OS|TMPLSET_MARK_USED_APP_LIST, NULL)))
		goto cleanup_0;
	string_list_clean(&apps);

	if ((rc = tmplset_get_marked_pkgs(tmpl, &args)))
		goto cleanup_0;

	/* create & init package manager wrapper */
	if ((rc = pm_init(ctid, &gc, &tc, tmpl, opts_vztt, &to)))
		goto cleanup_0;

	if ((rc = pm_set_root_dir(to, vc.ve_root)))
		goto cleanup_1;

	/* use only local (per-VE) veformat from VE private */
	if ((rc = pm_set_veformat(to, vc.veformat)))
		goto cleanup_1;

	/* add global custom exclude */
	if ((rc = pm_add_exclude(to, tc.exclude)))
		goto cleanup_1;
	/* add per_VE exclude */
	if ((rc = pm_add_exclude(to, vc.exclude)))
		goto cleanup_1;

	/* lock VE */
	if ((rc = lock_ve(ctid, opts_vztt->flags, &velockdata)))
		goto cleanup_1;

	/* Get installed vz packages list */
	if ((rc = pm_get_installed_vzpkg(to, vc.ve_private, &existed)))
		goto cleanup_2;

	/* ignore SIGINT */
	sigaction(SIGINT, NULL, &act_int);
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = SIG_IGN;
	sigaction(SIGINT, &act, NULL);

	/* lock template area on read */
	if ((rc = tmpl_lock(&gc, tmpl->base, 
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_3;

	/* Call pre-update script */
	if (!(opts_vztt->flags & OPT_VZTT_TEST)) {
	 	if ((rc = tmplset_run_ve_scripts(tmpl, ctid, vc.ve_root, \
				"pre-update", 0, opts_vztt->progress_fd)))
			goto cleanup_4;
	}

	/* Install packages into VE, it's the same as update, but install missed */
	if ((rc = pm_modify(to, VZPKG_INSTALL, &args, &added, &removed)))
		goto cleanup_4;

	/* Call post-update script */
	if (!(opts_vztt->flags & OPT_VZTT_TEST)) {
	 	if ((rc = tmplset_run_ve_scripts(tmpl, ctid, vc.ve_root, \
				"post-update", 0, opts_vztt->progress_fd)))
			goto cleanup_4;
	}
	tmpl_unlock(lockdata, opts_vztt->flags);

	/* fill output packages arrays */
	if (pkg_added)
		if ((rc = package_list_to_array(&added, pkg_added)))
			goto cleanup_3;
	if (pkg_removed)
		if ((rc = package_list_to_array(&removed, pkg_removed)))
			goto cleanup_3;

	/* if it's test transaction - success */
	if (opts_vztt->flags & OPT_VZTT_TEST)
		goto cleanup_3;

	if ((rc = merge_pkg_lists(&added, &removed, &existed)))
		goto cleanup_3;

	/* and save it */
	if ((rc = save_vzpackages(vc.ve_private, &existed)))
		goto cleanup_3;

	string_list_init(&apps);
	/* if application template autodetection is off,
	   do not change CT templates list during templates update */
	if (tc.apptmpl_autodetect) {
		/* process templates */
		tmplset_check_apps(tmpl, to, vc.ve_private, &existed, &apps);

		/* save new app templates set in VE config */
		if ((rc = ve_config1_save(vc.config, &apps)))
			goto cleanup_3;
	}

	goto cleanup_3;
cleanup_4:
	tmpl_unlock(lockdata, opts_vztt->flags);
cleanup_3:
	sigaction(SIGINT, &act_int, NULL);
cleanup_2:
	unlock_ve(ctid, velockdata, opts_vztt->flags);
cleanup_1:
	pm_clean(to);
cleanup_0:
	tmplset_clean(tmpl);

	string_list_clean(&args);
	string_list_clean(&apps);
	package_list_clean(&existed);
	package_list_clean(&removed);
	package_list_clean(&added);
	global_config_clean(&gc);
	vztt_config_clean(&tc);
	ve_config_clean(&vc);

	progress(progress_stage, 100, opts_vztt->progress_fd);

	return rc;
}

/* remove templates <tlist> from <veid> */
int vztt_remove_tmpl(
	const char *ctid,
	char *tlist[],
	size_t size,
	struct options *opts,
	struct package ***pkg_removed)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_remove_tmpl(ctid, tlist, size, opts_vztt, pkg_removed);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_remove_tmpl(
	const char *ctid,
	char *tlist[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_removed)
{
	int rc = 0;
	int lfound;
	void *lockdata, *velockdata;
	struct sigaction act;
	struct sigaction act_int;
	size_t i;

	/* list packages from removing templates */
	struct string_list rm_pkgs;
	/* list packages from OS and remaining app templates */
	struct string_list ve_pkgs;

	struct package_list added;
	struct package_list existed;
	struct package_list remains;
	struct package_list removed;

	struct string_list apps;

	struct string_list_el *r;
	struct package_list_el *v;

	struct ve_config vc;
	struct global_config gc;
	struct vztt_config tc;

	struct tmpl_set *tmpl;
	struct Transaction *to;
	char progress_stage[PATH_MAX];

	snprintf(progress_stage, sizeof(progress_stage),
		PROGRESS_REMOVE_APPTEMPLATES);
	for (i = 0; i < size; i++)
	{
		strncat(progress_stage, " ", sizeof(progress_stage) -
			strlen(progress_stage) - 1);
		strncat(progress_stage, tlist[i], sizeof(progress_stage) -
			strlen(progress_stage) - 1);
	}
	progress(progress_stage, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	ve_config_init(&vc);
	string_list_init(&rm_pkgs);
	string_list_init(&ve_pkgs);
	string_list_init(&apps);
	package_list_init(&added);
	package_list_init(&existed);
	package_list_init(&remains);
	package_list_init(&removed);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	if ((rc = check_n_load_ve_config(ctid, ENV_STATUS_RUNNING, &gc, &vc)))
		return rc;

	/* load corresponding os template in configs directory */
	if ((rc = tmplset_load(gc.template_dir, vc.ostemplate, &vc.templates, \
			TMPLSET_LOAD_APP_LIST, &tmpl, opts_vztt->flags)))
		return rc;

	/* Check for pkg operations allowed */
	if (tmpl->base->no_pkgs_actions || tmpl->os->no_pkgs_actions) {
		vztt_logger(0, 0, "The OS template this Container is based " \
			"on does not support operations with packages.");
		rc = VZT_TMPL_PKGS_OPS_NOT_ALLOWED;
		goto cleanup_0;
	}

	/* check & update metadata */
	if ((rc = update_metadata(vc.ostemplate, &gc, &tc, opts_vztt)))
		goto cleanup_0;

	for (i = 0; i < size; i++)
		string_list_add(&apps, tlist[i]);

	if ((rc = tmplset_check_for_remove(tmpl, &apps, opts_vztt->flags)))
		goto cleanup_0;

	if ((rc = tmplset_mark(tmpl, &apps, TMPLSET_MARK_USED_APP_LIST, NULL)))
		goto cleanup_0;
	string_list_clean(&apps);

	/* get packages for removing templates */
	if ((rc = tmplset_get_marked_pkgs(tmpl, &rm_pkgs)))
		goto cleanup_0;

	/* get packages for OS and remaining app templates */
	if ((rc = tmplset_get_ve_nonmarked_pkgs(tmpl, &ve_pkgs)))
		goto cleanup_0;

	/* create & init package manager wrapper */
	if ((rc = pm_init(ctid, &gc, &tc, tmpl, opts_vztt, &to)))
		goto cleanup_0;

	if ((rc = pm_set_root_dir(to, vc.ve_root)))
		goto cleanup_1;

	/* use only local (per-VE) veformat from VE private */
	if ((rc = pm_set_veformat(to, vc.veformat)))
		goto cleanup_1;

	/* lock VE */
	if ((rc = lock_ve(ctid, opts_vztt->flags, &velockdata)))
		goto cleanup_1;

	/* Get installed packages list */
	if ((rc = to->pm_get_install_pkg(to, &existed)))
		goto cleanup_2;

	/* find existed packages in OS and non-marked app templates
	   and remove such packages from any processing */
	for (v = existed.tqh_first; v != NULL; v = v->e.tqe_next) {
		lfound = 0;
		for (r = ve_pkgs.tqh_first; r != NULL; r = r->e.tqe_next) {
			if (to->pm_pkg_cmp(r->s, v->p) == 0) {
				lfound = 1;
				break;
			}
		}
		if (!lfound)
			package_list_add(&remains, v->p);
	}

	/* ignore SIGINT */
	sigaction(SIGINT, NULL, &act_int);
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = SIG_IGN;
	sigaction(SIGINT, &act, NULL);

	/* lock template area on read */
	if ((rc = tmpl_lock(&gc, tmpl->base, 
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_3;

	/* Call pre-remove script */
	if (!(opts_vztt->flags & OPT_VZTT_TEST)) {
	 	if ((rc = tmplset_run_ve_scripts(tmpl, ctid, vc.ve_root, \
				"pre-remove", 0, opts_vztt->progress_fd)))
			goto cleanup_4;
	}

	/* Install packages into VE */
	if ((rc = to->pm_remove_pkg(to, &rm_pkgs, &remains, &removed)))
		goto cleanup_4;

	/* Call post-remove script */
	if (!(opts_vztt->flags & OPT_VZTT_TEST)) {
	 	if ((rc = tmplset_run_ve_scripts(tmpl, ctid, vc.ve_root, \
				"post-remove", 0, opts_vztt->progress_fd)))
			goto cleanup_4;
	}
	tmpl_unlock(lockdata, opts_vztt->flags);

	/* fill output packages array */
	if (pkg_removed)
		if ((rc = package_list_to_array(&removed, pkg_removed)))
			goto cleanup_3;

	/* if it's test transaction - success */
	if (opts_vztt->flags & OPT_VZTT_TEST)
		goto cleanup_3;

	if ((rc = merge_pkg_lists(&added, &removed, &existed)))
		goto cleanup_3;

	/* and save it */
	if ((rc = save_vzpackages(vc.ve_private, &existed)))
		goto cleanup_3;

	/* process templates */
	string_list_init(&apps);
	/* if application template autodetection is off,
	   to remove directly removed templates only */
	if (tc.apptmpl_autodetect)
		tmplset_remove_marked_and_check_apps(tmpl, to,
				vc.ve_private, &existed, &apps);
	else
		tmplset_remove_marked_apps(tmpl, vc.ve_private, &apps);

	/* save new app templates set in VE config */
	if ((rc = ve_config1_save(vc.config, &apps)))
		goto cleanup_3;

	goto cleanup_3;
cleanup_4:
	tmpl_unlock(lockdata, opts_vztt->flags);
cleanup_3:
	sigaction(SIGINT, &act_int, NULL);
cleanup_2:
	unlock_ve(ctid, velockdata, opts_vztt->flags);
cleanup_1:
	pm_clean(to);
cleanup_0:
	tmplset_clean(tmpl);

	string_list_clean(&rm_pkgs);
	string_list_clean(&ve_pkgs);
	string_list_clean(&apps);
	package_list_clean(&added);
	package_list_clean(&existed);
	package_list_clean(&remains);
	package_list_clean(&removed);
	global_config_clean(&gc);
	vztt_config_clean(&tc);
	ve_config_clean(&vc);

	progress(progress_stage, 100, opts_vztt->progress_fd);

	return rc;
}

/* localinstall/localupdate <packages> into <veid> */
static int cmd_local(
	int cmd,
	const char *ctid,
	char *packages[],
	size_t npkg,
	struct options_vztt *opts_vztt,
	struct package ***pkg_added,
	struct package ***pkg_removed)
{
	int rc = 0;
	size_t i;
	void *lockdata, *velockdata;
	struct sigaction act;
	struct sigaction act_int;
	char buf[PATH_MAX+1];
	char cwd[PATH_MAX+1];
	char progress_stage[PATH_MAX];
	struct stat st;

	struct package_list existed;
	struct package_list removed;
	struct package_list added;

	struct string_list apps;
	struct string_list files;

	struct global_config gc;
	struct vztt_config tc;
	struct ve_config vc;

	struct tmpl_set *tmpl;
	struct Transaction *to;

	snprintf(progress_stage, sizeof(progress_stage), "%s",
		cmd == VZPKG_LOCALINSTALL ? PROGRESS_LOCALINSTALL :
		PROGRESS_LOCALUPDATE);
	progress(progress_stage, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	ve_config_init(&vc);
	string_list_init(&apps);
	string_list_init(&files);
	package_list_init(&added);
	package_list_init(&existed);
	package_list_init(&removed);

	if (!getcwd(cwd, sizeof(cwd)))
		return vztt_error(VZT_SYSTEM, errno, "getcwd()");

	/* check args */
	for (i = 0; i < npkg; i++) {
		if (*(packages[i]) != '/') {
			snprintf(buf, sizeof(buf), "%s/%s", cwd, packages[i]);
		} else {
			strncpy(buf, packages[i], sizeof(buf));
		}
		rc = stat(buf, &st);
		if (rc == ENOENT) {
			vztt_logger(0, errno, "File %s not found", buf);
			return VZT_FILE_NFOUND;
		} else if (rc) {
			vztt_logger(0, errno, "stat(\"%s\") error", buf);
			return VZT_CANT_LSTAT;
		}
		if (!S_ISREG(st.st_mode)) {
			vztt_logger(0, errno, "%s is not a regular file", buf);
			return VZT_BAD_PARAM;
		}
		if ((rc = string_list_add(&files, buf)))
			return rc;
	}

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	if ((rc = check_n_load_ve_config(ctid, ENV_STATUS_RUNNING, &gc, &vc)))
		return rc;

	/* load corresponding os template in configs directory */
	if ((rc = tmplset_load(gc.template_dir, vc.ostemplate, &vc.templates, \
			TMPLSET_LOAD_APP_LIST, &tmpl, opts_vztt->flags)))
		return rc;

	/* Check for pkg operations allowed */
	if (tmpl->base->no_pkgs_actions || tmpl->os->no_pkgs_actions) {
		vztt_logger(0, 0, "The OS template this Container is based " \
			"on does not support operations with packages.");
		rc = VZT_TMPL_PKGS_OPS_NOT_ALLOWED;
		goto cleanup_0;
	}

	/* check & update metadata */
	if ((rc = update_metadata(vc.ostemplate, &gc, &tc, opts_vztt)))
		goto cleanup_0;

	/* use repos of base, os and installed apps, do not use packages */
	if ((rc = tmplset_mark(tmpl, &vc.templates, \
			TMPLSET_MARK_OS|TMPLSET_MARK_USED_APP_LIST, NULL)))
		goto cleanup_0;

	/* create & init package manager wrapper */
	if ((rc = pm_init(ctid, &gc, &tc, tmpl, opts_vztt, &to)))
		goto cleanup_0;

	if ((rc = pm_set_root_dir(to, vc.ve_root)))
		goto cleanup_1;

	if ((rc = pm_set_veformat(to, vc.veformat)))
		goto cleanup_1;

	/* add global custom exclude */
	if ((rc = pm_add_exclude(to, tc.exclude)))
		goto cleanup_1;
	/* add per_VE exclude */
	if ((rc = pm_add_exclude(to, vc.exclude)))
		goto cleanup_1;

	/* lock VE */
	if ((rc = lock_ve(ctid, opts_vztt->flags, &velockdata)))
		goto cleanup_1;

	/* Get vz packages list */
	if ((rc = pm_get_installed_vzpkg(to, vc.ve_private, &existed)))
		goto cleanup_2;

	/* ignore SIGINT */
	sigaction(SIGINT, NULL, &act_int);
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = SIG_IGN;
	sigaction(SIGINT, &act, NULL);

	/* lock template area on read */
	if ((rc = tmpl_lock(&gc, tmpl->base, 
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_3;

	switch(cmd) {
		case VZPKG_LOCALINSTALL:
			/* Install packages into VE */
			if ((rc = to->pm_run_local(to, VZPKG_LOCALINSTALL,\
					&files, &added, &removed)))
				goto cleanup_4;
			break;
		case VZPKG_LOCALUPDATE:
			/* Install packages into VE */
			if ((rc = to->pm_run_local(to, VZPKG_LOCALUPDATE,\
					&files, &added, &removed)))
				goto cleanup_4;
			break;
	}
	tmpl_unlock(lockdata, opts_vztt->flags);

	/* fill output packages arrays */
	if (pkg_added)
		if ((rc = package_list_to_array(&added, pkg_added)))
			goto cleanup_3;
	if (pkg_removed)
		if ((rc = package_list_to_array(&removed, pkg_removed)))
			goto cleanup_3;

	/* if it's test transaction - success */
	if (opts_vztt->flags & OPT_VZTT_TEST)
		goto cleanup_3;

	if ((rc = merge_pkg_lists(&added, &removed, &existed)))
		goto cleanup_3;

	/* and save it */
	if ((rc = save_vzpackages(vc.ve_private, &existed)))
		goto cleanup_3;

	/* if application template autodetection is off,
	   do not change CT templates list during packages localinstall */
	if (tc.apptmpl_autodetect) {
		/* process templates */
		tmplset_check_apps(tmpl, to, vc.ve_private, &existed, &apps);

		/* save new templates list <apps> in config */
		if ((rc = ve_config2_save(vc.config, &apps)))
			goto cleanup_3;
	}

	goto cleanup_3;
cleanup_4:
	tmpl_unlock(lockdata, opts_vztt->flags);
cleanup_3:
	sigaction(SIGINT, &act_int, NULL);
cleanup_2:
	unlock_ve(ctid, velockdata, opts_vztt->flags);
cleanup_1:
	pm_clean(to);
cleanup_0:
	tmplset_clean(tmpl);

	string_list_clean(&apps);
	string_list_clean(&files);
	package_list_clean(&added);
	package_list_clean(&existed);
	package_list_clean(&removed);
	global_config_clean(&gc);
	vztt_config_clean(&tc);
	ve_config_clean(&vc);

	progress(progress_stage, 100, opts_vztt->progress_fd);

	return rc;
}

/* install local packages */
int vztt_localinstall(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options *opts,
	struct package ***pkg_added,
	struct package ***pkg_removed)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = cmd_local(VZPKG_LOCALINSTALL, ctid, packages, size, opts_vztt, \
		pkg_added, pkg_removed);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_localinstall(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_added,
	struct package ***pkg_removed)
{
	return cmd_local(VZPKG_LOCALINSTALL, ctid, packages, size, opts_vztt, \
		pkg_added, pkg_removed);
}

/* update local packages */
int vztt_localupdate(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options *opts,
	struct package ***pkg_added,
	struct package ***pkg_removed)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = cmd_local(VZPKG_LOCALUPDATE, ctid, packages, size, opts_vztt, \
		pkg_added, pkg_removed);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_localupdate(
	const char *ctid,
	char *packages[],
	size_t size,
	struct options_vztt *opts_vztt,
	struct package ***pkg_added,
	struct package ***pkg_removed)
{
	return cmd_local(VZPKG_LOCALUPDATE, ctid, packages, size, opts_vztt, \
		pkg_added, pkg_removed);
}

/* clean package list */
void vztt_clean_packages_list(struct package **ls)
{
	size_t i;
	if (ls == NULL)
		return;

	for (i = 0; ls[i]; i++) {
		VZTT_FREE_STR(ls[i]->name);
		VZTT_FREE_STR(ls[i]->arch);
		VZTT_FREE_STR(ls[i]->evr);
		VZTT_FREE_STR(ls[i]->descr);
		free((void *)ls[i]);
	}
	free((void *)ls);
}

