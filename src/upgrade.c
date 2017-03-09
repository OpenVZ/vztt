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
 * VE package set upgrade
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
#include "transaction.h"
#include "config.h"
#include "lock.h"
#include "progress_messages.h"

/* upgrade VE package set only */
int ve_packages_upgrade(
	const char *ctid,
	struct ve_config *vconfig,
	struct Transaction *t,
	struct tmpl_set *tmpl,
	struct options_vztt *opts_vztt,
	struct package_list *removed,
	struct package_list *updated,
	struct package_list *added,
	struct package_list *toconvert,
	struct string_list *apps)
{
	int rc;

	struct string_list target_packages;
	struct string_list args;
	struct string_list_el *p;

	/* old installed packages list */
	struct package_list existed;
	/* new full list of vz packages of VE */
	struct package_list packages;
	struct package_list_el *pkg;

	string_list_init(&target_packages);
	string_list_init(&args);
	package_list_init(&existed);
	package_list_init(&packages);
	t->pm_fix_pkg_db(t);

	/* Step 2: Get installed packages list via internal package manager */
	if ((rc = t->pm_get_install_pkg(t, &existed)))
		return rc;

	/* Clean internal package DB cache */
	t->pm_fix_pkg_db(t);

	/* Step 3: Upgrade all packages in VE */
	if ((rc = pm_modify(t, VZPKG_UPGRADE, &args, updated, removed))) {
		t->pm_fix_pkg_db(t);
		return rc;
	}

	if (opts_vztt->flags & OPT_VZTT_TEST) {
		vztt_logger(0, 0, "Upgrade CT %s to %s is possible.", \
			ctid, tmpl->os->name);
		return 0;
	}

	/* Step 4: Install not installed packages from target EZ templates into VE */
	/* get full packages list for target EZ OS template */
	tmplset_get_marked_pkgs(tmpl, &target_packages);
	/* get not installed yet packages from target templates */
	for (p = target_packages.tqh_first; p != NULL; p = p->e.tqe_next) {
		/* remove from target packages already all existed packages,
		we already updated/obsoleted them, if it were possible */
		if (pm_find_in_list(t, &existed, p->s) == 0) continue;
/*		if (pm_find_in_list(t, updated, p->s) == 0) continue;
		if (pm_find_in_list(t, removed, p->s) == 0) continue;*/
		string_list_add(&args, p->s);
	}

	if (!string_list_empty(&args)) {
		/* complete install all target EZ templates packages */
		if ((rc = pm_modify(t, VZPKG_INSTALL, &args, added, removed)))
			return rc;
	}

	/* Clean internal package DB cache */
	t->pm_fix_pkg_db(t);

	/* set rootdir in tmpdir */
	if ((rc = pm_create_tmp_root(t)))
		return rc;

	/* Step 5: Repair EZ template area */
	/* find unupdated packages (points to old template area) */
	for (pkg = existed.tqh_first; pkg != NULL; \
			pkg = pkg->e.tqe_next) {
		/* find this package in updated */
		if (package_list_find(updated, pkg->p))
			continue;

		/* find this package in added */
		if (package_list_find(added, pkg->p))
			continue;

		/* find this package in removed */
		if (package_list_find(removed, pkg->p))
			continue;

		/* this package was not updated and was not removed */
		/* find suitable package directory in target template area
		 or try to download it if not found */
		if (pm_prepare_pkg_area(t, pkg->p) == 0)
			if ((rc = package_list_add(toconvert, pkg->p)))
				return rc;
	}

	/* ok - upgrade completed. create new full vz packages list */
	if ((rc = package_list_copy(&packages, updated)))
		return rc;
	if ((rc = package_list_copy(&packages, toconvert)))
		return rc;
	if ((rc = package_list_copy(&packages, added)))
		return rc;

	/* create privdir/templates/vzpackages */
	if ((rc = save_vzpackages(vconfig->ve_private, &packages)))
		return rc;

	/* select installed app templates via vz packages list */
	tmplset_get_installed_apps(tmpl, t, vconfig->ve_private, &packages, apps);

	package_list_clean(&existed);
	/* package is copy of other lists */
	package_list_clean(&packages);
	string_list_clean(&target_packages);
	string_list_clean(&args);
	return 0;
}

/* upgrade ve from one EZ OS template to other */
int vztt_upgrade(
	char *arg,
	struct options *opts,
	struct package ***pkg_updated,
	struct package ***pkg_added,
	struct package ***pkg_removed,
	struct package ***pkg_converted)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_upgrade(arg, opts_vztt, pkg_updated, pkg_added, pkg_removed,
			pkg_converted);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_upgrade(
	char *arg,
	struct options_vztt *opts_vztt,
	struct package ***pkg_updated,
	struct package ***pkg_added,
	struct package ***pkg_removed,
	struct package ***pkg_converted)
{
	int rc = 0;
	char upgr_template[PATH_MAX];
	ctid_t ctid;
	void *lockdata, *velockdata;

	struct ve_config vc;
	struct global_config gc;
	struct vztt_config tc;

	/* common app templates list for this OS template */
	struct string_list apps;
	/* list of removed packages (obsoletes during update) */
	struct package_list removed;
	/* list of new packages, installed as updates of existed */
	struct package_list updated;
	/* list of new installed packages */
	struct package_list added;
	/* list of packages from existed which will to convert 
	from source to target template area */
	struct package_list toconvert;

	struct tmpl_set *s_tmpl;
	struct tmpl_set *t_tmpl;
	struct Transaction *to;

	progress(PROGRESS_UPGRADE, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	ve_config_init(&vc);
	string_list_init(&apps);
	package_list_init(&removed);
	package_list_init(&updated);
	package_list_init(&added);
	package_list_init(&toconvert);

	if ((rc = get_veid(arg, ctid)))
		return rc;

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
			TMPLSET_LOAD_APP_LIST, &s_tmpl, opts_vztt->flags)))
		return rc;

	/* Check for pkg operations allowed */
	if (s_tmpl->base->no_pkgs_actions || s_tmpl->os->no_pkgs_actions) {
		vztt_logger(0, 0, "The OS template this Container is based " \
			"on does not support operations with packages.");
		rc = VZT_TMPL_PKGS_OPS_NOT_ALLOWED;
		goto cleanup_0;
	}

	/* now try to find ostemplate with osname & osarch 
	and upgradable_version == osver or osver = osver+1 */
	if ((rc = tmplset_find_upgrade(s_tmpl,
			(opts_vztt->flags & OPT_VZTT_FORCE),
			opts_vztt->objects == OPT_OBJECTS_PACKAGES, &t_tmpl)))
		goto cleanup_0;

	/* Give os template name we upgrade to */
	snprintf(upgr_template, PATH_MAX, "%s-%s-%s", t_tmpl->base->osname, \
		t_tmpl->base->osver, t_tmpl->base->osarch);

	/* check & update metadata */
	if ((rc = update_metadata(vc.ostemplate, &gc, &tc, opts_vztt)))
		goto cleanup_1;

	if ((rc = update_metadata(upgr_template, &gc, &tc, opts_vztt)))
		goto cleanup_1;

	/* We should restart Container is case when template that we upgrade
	    to requires especial osrelease */

	/* lock VE */
	if ((rc = lock_ve(ctid, opts_vztt->flags, &velockdata)))
		goto cleanup_1;

	if (t_tmpl->base->osrelease &&
		strlen(t_tmpl->base->osrelease)) {
		/* Restart VPS */
		char *set_osrelease[] = {VZCTL, "--skiplock", "--quiet",
			"restart", ctid, "--osrelease",
			t_tmpl->base->osrelease, NULL};
		execv_cmd_logger(2, 0, set_osrelease);
		if ((rc = execv_cmd(set_osrelease, 1, 1)))
			goto cleanup_2;
	}

	if ((rc = tmplset_mark(t_tmpl, NULL, \
			TMPLSET_MARK_OS|TMPLSET_MARK_USED_APP_LIST, NULL)))
		goto cleanup_2;

	/* create & init package manager wrapper */
	if ((rc = pm_init(ctid, &gc, &tc, t_tmpl, opts_vztt, &to)))
		goto cleanup_2;

	if ((rc = pm_set_root_dir(to, vc.ve_root)))
		goto cleanup_3;

	/* use only local (per-VE) veformat from VE private */
	if ((rc = pm_set_veformat(to, vc.veformat)))
		goto cleanup_3;

	/* add global custom exclude */
	if ((rc = pm_add_exclude(to, tc.exclude)))
		goto cleanup_3;
	/* add per_VE exclude */
	if ((rc = pm_add_exclude(to, vc.exclude)))
		goto cleanup_3;

	/* lock target template area on read */
	if ((rc = tmpl_lock(&gc, t_tmpl->base, 
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_3;

	if (!(opts_vztt->flags & OPT_VZTT_TEST)) {
		/* Step 1: Call pre-upgrade script. */
 		if ((rc = tmplset_run_ve_scripts(t_tmpl, ctid, vc.ve_root, \
				"pre-upgrade", 0, opts_vztt->progress_fd)))
			goto cleanup_4;
	}

	if ((rc = ve_packages_upgrade(ctid, &vc, to, t_tmpl, opts_vztt, \
				&removed, &updated, &added, &toconvert, &apps)))
		goto cleanup_4;

	/* fill output packages arrays */
	if (pkg_updated)
		if ((rc = package_list_to_array(&updated, pkg_updated)))
			goto cleanup_4;
	if (pkg_added)
		if ((rc = package_list_to_array(&added, pkg_added)))
			goto cleanup_4;
	if (pkg_removed)
		if ((rc = package_list_to_array(&removed, pkg_removed)))
			goto cleanup_4;
	if (pkg_converted)
		if ((rc = package_list_to_array(&toconvert, pkg_converted)))
			goto cleanup_4;

	/* if it's test mode - success */
	if (opts_vztt->flags & OPT_VZTT_TEST)
		goto cleanup_4;

	/* stop VPS */
	if ((rc = do_vzctl("stop", ctid, 1, DO_VZCTL_QUIET |
		DO_VZCTL_FAST | DO_VZCTL_LOGGER)))
		goto cleanup_4;

	/* try to fix rpm database */
	to->pm_fix_pkg_db(to);

	/* modify VE config */
	unsigned long technologies;
 
	/* save used technologies */
	technologies = get_ve_technologies(t_tmpl->base->technologies, \
			vc.technologies);
	
	/* and save in config */
	if ((rc = ve_config6_save(vc.config,
			t_tmpl->os->name,
			t_tmpl->base->distribution,
			&apps,
			technologies,
			vc.veformat)))
		goto cleanup_4;

	/* start VPS */
	if ((rc = do_vzctl("start", ctid, 1, DO_VZCTL_QUIET |
		DO_VZCTL_WAIT | DO_VZCTL_LOGGER)))
		goto cleanup_4;

	/* Call post-upgrade script */
 	if ((rc = tmplset_run_ve_scripts(t_tmpl, ctid, vc.ve_root, \
			"post-upgrade", 0, opts_vztt->progress_fd)))
		goto cleanup_4;

	/* Restart VPS */
	rc = do_vzctl("restart", ctid, 1, DO_VZCTL_QUIET |
		DO_VZCTL_WAIT | DO_VZCTL_LOGGER);

cleanup_4:
	tmpl_unlock(lockdata, opts_vztt->flags);
cleanup_3:
	pm_clean(to);
cleanup_2:
	unlock_ve(ctid, velockdata, opts_vztt->flags);
cleanup_1:
	tmplset_clean(t_tmpl);
cleanup_0:
	tmplset_clean(s_tmpl);

	string_list_clean(&apps);
	package_list_clean(&removed);
	package_list_clean(&updated);
	package_list_clean(&added);
	package_list_clean(&toconvert);
	global_config_clean(&gc);
	vztt_config_clean(&tc);
	ve_config_clean(&vc);

	progress(PROGRESS_UPGRADE, 100, opts_vztt->progress_fd);

	return rc;
}

