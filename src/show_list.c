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
 * vztt list functions
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
#include <vzctl/libvzctl.h>

#include "vztt.h"
#include "util.h"
#include "tmplset.h"
#include "config.h"
#include "transaction.h"
#include "lock.h"
#include "list_avail.h"
#include "progress_messages.h"

/*
 get list of `custom` packages:
 packages, installed in VE but not available in repositories
*/
int vztt_get_custom_pkgs(
		const char *ctid,
		struct options *opts,
		struct package ***packages)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_get_custom_pkgs(ctid, opts_vztt, packages);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_get_custom_pkgs(
		const char *ctid,
		struct options_vztt *opts_vztt,
		struct package ***packages)
{
	int rc = 0;
	void *lockdata;

	struct package_list ls;

	/* list of packages in VE */
	struct package_list installed;
	/* list of of available in repos packages */
	struct package_list available;
	/* target os template structure */
	struct package_list_el *p;

	struct Transaction *t;
	struct tmpl_set *tmpl;

	struct global_config gc;
	struct vztt_config tc;
	struct ve_config vc;

	progress(PROGRESS_GET_CUSTOM_PACKAGES, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	ve_config_init(&vc);
	package_list_init(&installed);
	package_list_init(&available);
	package_list_init(&ls);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	if ((rc = check_n_load_ve_config(ctid, ENV_STATUS_EXISTS, &gc, &vc)))
		return rc;

	/* check & update metadata */
	if ((rc = update_metadata(vc.ostemplate, &gc, &tc, opts_vztt)))
		return rc;

	/* load corresponding os template in configs directory */
	if ((rc = tmplset_load(gc.template_dir, vc.ostemplate, &vc.templates,
			TMPLSET_LOAD_APP_LIST, &tmpl, opts_vztt->flags)))
		return rc;

	/* use repos of base, os and installed apps, do not use packages */
	if ((rc = tmplset_mark(tmpl, &vc.templates, \
			TMPLSET_MARK_OS|TMPLSET_MARK_USED_APP_LIST, NULL)))
		goto cleanup_0;

	/* create & init package manager wrapper */
	if ((rc = pm_init(ctid, &gc, &tc, tmpl, opts_vztt, &t)))
		goto cleanup_0;

	if ((rc = pm_set_root_dir(t, vc.ve_root)))
		goto cleanup_1;

	/* add global custom exclude */
	if ((rc = pm_add_exclude(t, tc.exclude)))
		goto cleanup_1;
	/* add per_VE exclude */
	if ((rc = pm_add_exclude(t, vc.exclude)))
		goto cleanup_1;

	if ((rc = pm_get_installed_pkg_from_ve(t, ctid, &installed)))
		goto cleanup_1;

	if ((rc = tmpl_lock(&gc, tmpl->base,
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_1;
	rc = pm_get_available(t, &installed, &available);
	tmpl_unlock(lockdata, opts_vztt->flags);
	if (rc)
		goto cleanup_1;

	/* compare input packages and packages from repos */
	for (p = installed.tqh_first; p != NULL; p = p->e.tqe_next) {
		if (package_list_find(&available, p->p))
			continue;
		package_list_add(&ls, p->p);
	}

	/* copy string list <ls> to string array <*a> */
	if ((rc = package_list_to_array(&ls, packages)))
		goto cleanup_1;

cleanup_1:
	/* remove temporary dir */
	pm_clean(t);
cleanup_0:
	tmplset_clean(tmpl);

	package_list_clean(&ls);
	package_list_clean_all(&installed);
	package_list_clean_all(&available);
	global_config_clean(&gc);
	vztt_config_clean(&tc);
	ve_config_clean(&vc);

	progress(PROGRESS_GET_CUSTOM_PACKAGES, 100, opts_vztt->progress_fd);

	return rc;
}

/* get list of installed into VE <ctid> packages as char ***<packages> */
int vztt_get_ve_pkgs(
		const char *ctid,
		struct options *opts,
		struct package ***packages)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_get_ve_pkgs(ctid, opts_vztt, packages);
	vztt_options_free(opts_vztt);
	return rc;
}

/* get list of installed into VE <ctid> packages as char ***<packages> */
int vztt2_get_ve_pkgs(
		const char *ctid,
		struct options_vztt *opts_vztt,
		struct package ***packages)
{
	int rc = 0;

	/* list of packages installed in VE */
	struct package_list installed;

	struct ve_config vc;
	struct global_config gc;
	struct vztt_config tc;

	struct Transaction *t;
	struct tmpl_set *tmpl;

	progress(PROGRESS_GET_CONTAINER_PACKAGES, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	ve_config_init(&vc);
	package_list_init(&installed);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	if ((rc = check_n_load_ve_config(ctid, ENV_STATUS_EXISTS, &gc, &vc)))
		return rc;

	/* load corresponding os template in configs directory */
	if ((rc = tmplset_load(gc.template_dir, vc.ostemplate, \
			&vc.templates, TMPLSET_LOAD_APP_LIST, &tmpl, \
			opts_vztt->flags)))
		return rc;

	/* create & init package manager wrapper */
	if ((rc = pm_init(ctid, &gc, &tc, tmpl, opts_vztt, &t)))
		goto cleanup_0;

	if ((rc = pm_set_root_dir(t, vc.ve_root)))
		goto cleanup_1;

	if ((rc = pm_get_installed_pkg_from_ve(t, ctid, &installed)))
		goto cleanup_1;

	/* copy string list <ls> to string array <*a> */
	if ((rc = package_list_to_array(&installed, packages)))
		goto cleanup_1;

cleanup_1:
	/* remove temporary dir */
	pm_clean(t);
cleanup_0:
	tmplset_clean(tmpl);

	/* and clean list */
	package_list_clean(&installed);
	global_config_clean(&gc);
	vztt_config_clean(&tc);
	ve_config_clean(&vc);

	progress(PROGRESS_GET_CONTAINER_PACKAGES, 100, opts_vztt->progress_fd);

	return rc;
}

/* get list of installed into VE <ctid> groups of packages */
int vztt_get_ve_groups(
	const char *ctid,
	struct options *opts,
	struct group_list *groups)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_get_ve_groups(ctid, opts_vztt, groups);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_get_ve_groups(
	const char *ctid,
	struct options_vztt *opts_vztt,
	struct group_list *groups)
{
	int rc = 0;
	void *lockdata;
	char buf[PATH_MAX+1];
	vzctl_env_status_t ve_status;

	struct ve_config vc;
	struct global_config gc;
	struct vztt_config tc;

	struct Transaction *t;
	struct tmpl_set *tmpl;

	progress(PROGRESS_GET_CONTAINER_GROUPS, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	ve_config_init(&vc);

	groups->available = NULL;
	groups->installed = NULL;

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	if ((rc = check_n_load_ve_config(ctid, ENV_STATUS_EXISTS, &gc, &vc)))
		return rc;

	/* get VE status */
	if (vzctl2_get_env_status(ctid, &ve_status, ENV_STATUS_ALL))
		vztt_error(VZT_VZCTL_ERROR, 0, "Can't get status of CT %d: %s",
			ctid, vzctl2_get_last_error());

	/* load corresponding os template in configs directory */
	if ((rc = tmplset_load(gc.template_dir, vc.ostemplate, \
			&vc.templates, TMPLSET_LOAD_APP_LIST, &tmpl, \
			opts_vztt->flags)))
		return rc;

	/* create & init package manager wrapper */
	if ((rc = pm_init(ctid, &gc, &tc, tmpl, opts_vztt, &t)))
		goto cleanup_0;

	if (~ve_status.mask & ENV_STATUS_RUNNING)
		/* only not runned VE - read packages db from private */
		get_ve_private_root(vc.ve_private, vc.layout, buf, sizeof(buf));
	else
		strncpy(buf, vc.ve_root, sizeof(buf));
	if ((rc = pm_set_root_dir(t, buf)))
		goto cleanup_1;

	if ((rc = tmpl_lock(&gc, tmpl->base, LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_1;
	rc = t->pm_get_group_list(t, groups);
	tmpl_unlock(lockdata, opts_vztt->flags);

cleanup_1:
	/* remove temporary dir */
	pm_clean(t);
cleanup_0:
	tmplset_clean(tmpl);

	global_config_clean(&gc);
	vztt_config_clean(&tc);
	ve_config_clean(&vc);

	progress(PROGRESS_GET_CONTAINER_GROUPS, 100, opts_vztt->progress_fd);

	return rc;
}


/* get list of packages directories from template area, used for VE <ctid> */
int vztt_get_vzdir(const char *ctid, struct options *opts, char ***vzdir)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_get_vzdir(ctid, opts_vztt, vzdir);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_get_vzdir(const char *ctid, struct options_vztt *opts_vztt, char ***vzdir)
{
	int rc = 0;
	char buf[PATH_MAX+1];
	char path[PATH_MAX+1];
	void *lockdata;

	struct string_list ls;

	/* list of vzpackages installed in VE */
	struct package_list vzpackages;
	struct package_list_el *p;

	struct ve_config vc;
	struct global_config gc;
	struct vztt_config tc;

	struct Transaction *t;
	struct tmpl_set *tmpl;

	progress(PROGRESS_GET_PACKAGE_DIRS, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	ve_config_init(&vc);
	package_list_init(&vzpackages);
	string_list_init(&ls);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	if ((rc = check_n_load_ve_config(ctid, ENV_STATUS_EXISTS, &gc, &vc)))
		return rc;

	if (vc.veformat == VZ_T_VZFS0)
		/* return vzdir with first NULL to avoid fault on broken array */
		return string_list_to_array(&ls, vzdir);

	/* load corresponding os template in configs directory */
	if ((rc = tmplset_load(gc.template_dir, vc.ostemplate, \
			&vc.templates, TMPLSET_LOAD_APP_LIST, &tmpl, \
			opts_vztt->flags)))
		return rc;

	/* create & init package manager wrapper */
	if ((rc = pm_init(ctid, &gc, &tc, tmpl, opts_vztt, &t)))
		goto cleanup_0;

	if ((rc = pm_set_root_dir(t, vc.ve_root)))
		goto cleanup_1;

	/* show only VZ packages */
	/* get list of vz packages from file */
	if (opts_vztt->flags & OPT_VZTT_SKIP_DB) {
		rc = pm_get_installed_vzpkg(t, vc.ve_private, &vzpackages);
	} else {
		rc = pm_get_installed_vzpkg2(t, ctid, vc.ve_private, &vzpackages);
	}
	if (rc)
		goto cleanup_1;

	/* lock template area on read */
	if ((rc = tmpl_lock(&gc, tmpl->base,
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_1;

	for (p = vzpackages.tqh_first; p != NULL; p = p->e.tqe_next) {
		if (!t->pm_find_pkg_area_ex(t, p->p, buf, sizeof(buf))) {
			/* VE private area is invalid, but will ignore it */
			continue;
		}
		snprintf(path, sizeof(path), "%s/%s/%s", \
			gc.template_dir, tmpl->base->basesubdir, buf);
		string_list_add(&ls, path);
	}
	tmpl_unlock(lockdata, opts_vztt->flags);

	/* copy string list <ls> to string array <*a> */
	if ((rc = string_list_to_array(&ls, vzdir)))
		goto cleanup_1;

cleanup_1:
	/* remove temporary dir */
	pm_clean(t);
cleanup_0:
	tmplset_clean(tmpl);
	/* and clean list */
	string_list_clean(&ls);
	package_list_clean_all(&vzpackages);
	global_config_clean(&gc);
	vztt_config_clean(&tc);
	ve_config_clean(&vc);

	progress(PROGRESS_GET_PACKAGE_DIRS, 100, opts_vztt->progress_fd);

	return rc;
}

/* get list of available packages for template <tname> */
int vztt_get_template_pkgs(
		char *tname,
		struct options *opts,
		struct package ***packages)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_get_template_pkgs(tname, opts_vztt, packages);
	vztt_options_free(opts_vztt);
	return rc;
}

/* get list of available packages for template <tname> */
int vztt2_get_template_pkgs(
		char *tname,
		struct options_vztt *opts_vztt,
		struct package ***packages)
{
	int rc = 0;
	void *lockdata;

	struct package_list available;
	struct package_list empty;

	struct global_config gc;
	struct vztt_config tc;

	struct Transaction *to;
	struct tmpl_set *tmpl;

	progress(PROGRESS_GET_TEMPLATE_PKGS, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	package_list_init(&empty);
	package_list_init(&available);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	/* check & update metadata */
	if ((rc = update_metadata(tname, &gc, &tc, opts_vztt)))
		return rc;

	/* load all OS and app templates */
	if ((rc = tmplset_load(gc.template_dir, tname, NULL, \
			TMPLSET_LOAD_OS_LIST|TMPLSET_LOAD_APP_LIST, &tmpl, \
			opts_vztt->flags)))
		return rc;

	/* mark all - to use all available repos */
	if ((rc = tmplset_mark(tmpl, NULL, \
			TMPLSET_MARK_OS | \
			TMPLSET_MARK_OS_LIST | \
			TMPLSET_MARK_AVAIL_APP_LIST, NULL)))
		goto cleanup_0;

	/* create & init package manager wrapper */
	if ((rc = pm_init(0, &gc, &tc, tmpl, opts_vztt, &to)))
		goto cleanup_0;

	if ((rc = pm_create_tmp_root(to)))
		goto cleanup_1;

	if ((rc = tmpl_lock(&gc, tmpl->base,
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_1;

	rc = pm_modify(to, VZPKG_LIST, NULL, &available, &empty);
	tmpl_unlock(lockdata, opts_vztt->flags);
	if (rc)
		goto cleanup_1;

	/* copy string list <ls> to string array */
	if ((rc = package_list_to_array(&available, packages)))
		goto cleanup_1;

cleanup_1:
	/* remove temporary dir */
	pm_clean(to);
cleanup_0:
	tmplset_clean(tmpl);
	/* and clean list */
	package_list_clean_all(&empty);
	package_list_clean_all(&available);
	global_config_clean(&gc);
	vztt_config_clean(&tc);

	progress(PROGRESS_GET_TEMPLATE_PKGS, 100, opts_vztt->progress_fd);

	return rc;
}

/* get list of available groups of packages for template <tname> */
int vztt_get_template_groups(
		char *tname,
		struct options *opts,
		struct group_list *groups)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_get_template_groups(tname, opts_vztt, groups);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_get_template_groups(
		char *tname,
		struct options_vztt *opts_vztt,
		struct group_list *groups)
{
	int rc = 0;
	void *lockdata;

	struct global_config gc;
	struct vztt_config tc;

	struct Transaction *to;
	struct tmpl_set *tmpl;

	progress(PROGRESS_GET_TEMPLATE_GROUPS, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);

	groups->available = NULL;
	groups->installed = NULL;

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	/* check & update metadata */
	if ((rc = update_metadata(tname, &gc, &tc, opts_vztt)))
		return rc;

	/* load all OS and app templates */
	if ((rc = tmplset_load(gc.template_dir, tname, NULL, \
			TMPLSET_LOAD_OS_LIST|TMPLSET_LOAD_APP_LIST, &tmpl, \
			opts_vztt->flags)))
		return rc;

	/* mark all - to use all available repos */
	if ((rc = tmplset_mark(tmpl, NULL,
			TMPLSET_MARK_OS | TMPLSET_MARK_OS_LIST | TMPLSET_MARK_AVAIL_APP_LIST, NULL)))
		goto cleanup_0;

	/* create & init package manager wrapper */
	if ((rc = pm_init(0, &gc, &tc, tmpl, opts_vztt, &to)))
		goto cleanup_0;

	if ((rc = pm_create_tmp_root(to)))
		goto cleanup_1;

	if ((rc = tmpl_lock(&gc, tmpl->base, LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_1;
	rc = to->pm_get_group_list(to, groups);
	tmpl_unlock(lockdata, opts_vztt->flags);

cleanup_1:
	/* remove temporary dir */
	pm_clean(to);
cleanup_0:
	tmplset_clean(tmpl);
	global_config_clean(&gc);
	vztt_config_clean(&tc);

	progress(PROGRESS_GET_TEMPLATE_GROUPS, 100, opts_vztt->progress_fd);

	return rc;
}

/* clean group list */
void vztt_clean_group_list(struct group_list *groups)
{
	size_t i;
	if (groups == NULL)
		return;
	if (groups->available) {
		for (i = 0; groups->available[i]; i++)
			free(groups->available[i]);
		free((void *)groups->available);
	}
	if (groups->installed) {
		for (i = 0; groups->installed[i]; i++)
			free(groups->installed[i]);
		free((void *)groups->installed);
	}
}

/* get templates list for <ostemplate> */
int vztt_get_templates_list(
		char *ostemplate,
		struct options *opts,
		struct tmpl_list_el ***ls)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_get_templates_list(ostemplate, opts_vztt, ls);
	vztt_options_free(opts_vztt);
	return rc;
}

/* get templates list for <ostemplate> */
int vztt2_get_templates_list(
		char *ostemplate,
		struct options_vztt *opts_vztt,
		struct tmpl_list_el ***ls)
{
	int rc = 0;
	struct global_config gc;
	struct vztt_config tc;

	struct tmpl_set *tmpl;

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

        if (opts_vztt->flags & OPT_VZTT_AVAILABLE)
            return list_avail_get_list( ostemplate, ls , opts_vztt->templates );

	/* load all OS and app templates */
	if ((rc = tmplset_selective_load(gc.template_dir, ostemplate, NULL,\
			TMPLSET_LOAD_OS_LIST|TMPLSET_LOAD_APP_LIST, &tmpl,
			opts_vztt)))
		return rc;

	rc = tmplset_get_list(tmpl, &gc, opts_vztt, ls);

	tmplset_clean(tmpl);
	global_config_clean(&gc);
	vztt_config_clean(&tc);

	return rc;
}

/* clean template list */
void vztt_clean_templates_list(struct tmpl_list_el **ls)
{
	size_t i;
	if (ls == NULL)
		return;

	for (i = 0; ls[i]; i++) {
		VZTT_FREE_STR(ls[i]->timestamp);
		vztt_clean_tmpl_info(ls[i]->info);
		free((void *)ls[i]->info);
		free((void *)ls[i]);
	}
	free((void *)ls);
}

/* get template list for ve <ctid> */
int vztt_get_ve_templates_list(
	const char *ctid,
	struct options *opts,
	struct tmpl_list_el ***ls)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_get_ve_templates_list(ctid, opts_vztt, ls);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_get_ve_templates_list(
	const char *ctid,
	struct options_vztt *opts_vztt,
	struct tmpl_list_el ***ls)
{
	int rc = 0;

	struct ve_config vc;
	struct global_config gc;
	struct vztt_config tc;

	struct tmpl_set *tmpl;

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	ve_config_init(&vc);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	if ((rc = check_n_load_ve_config(ctid, ENV_STATUS_EXISTS, &gc, &vc)))
		return rc;

	/* load corresponding os template in configs directory */
	if ((rc = tmplset_selective_load(gc.template_dir, vc.ostemplate, \
			&vc.templates, TMPLSET_LOAD_APP_LIST, &tmpl, \
			opts_vztt)))
		return rc;

	rc = tmplset_get_ve_list(tmpl, &gc, vc.ve_private, opts_vztt, ls);

	/* On success we have list of templates installed on the node,
	 * but not installed (=available for install) into VE
	 *
	 * Let add list of templates available in external repositories
	 * for install to the node (and then to VE)
	 *  */
	if (!rc && (opts_vztt->flags & OPT_VZTT_AVAILABLE)) {
		int rc2;
		struct tmpl_list_el ** al;
		if ((rc2 = list_avail_get_list(vc.ostemplate, &al, opts_vztt->templates)))
			rc = rc2;
		else
			if (al[0]) /* 'List' is not empty */ {
			int i;
			size_t sz1 = 0, sz2 = 0;
			struct tmpl_list_el ** combined_list;
			for (sz1 = 0; (*ls)[sz1]; sz1++);
			for (sz2 = 0; al[sz2]; sz2++);
			rc = tmplset_alloc_list(sz1 + sz2, &combined_list);
			if (!rc) {
				for (sz1 = 0; (*ls)[sz1]; sz1++)
					combined_list[sz1] = (*ls)[sz1];
				for (i = 0; al[i]; i++)
					combined_list[sz1 + i] = al[i];
				free(al);
				free(*ls);
				*ls = combined_list;
			}
		}
	}

	tmplset_clean(tmpl);
	global_config_clean(&gc);
	vztt_config_clean(&tc);
	ve_config_clean(&vc);

	return rc;
}

/* for backup: get list of installed apptemplates via vzpackages list from <veprivate>
   and TEMPLATE variables from <veconfig> */
int vztt_get_backup_apps(
		char *veprivate,
		char *veconfig,
		struct options *opts,
		char ***arr)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_get_backup_apps(veprivate, veconfig, opts_vztt, arr);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_get_backup_apps(
		char *veprivate,
		char *veconfig,
		struct options_vztt *opts_vztt,
		char ***arr)
{
	int rc = 0;

	/* list of vzpackages installed in VE */
	struct package_list vzpackages;

	struct string_list apps;

	struct ve_config vc;
	struct global_config gc;
	struct vztt_config tc;

	struct Transaction *t;
	struct tmpl_set *tmpl;

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	package_list_init(&vzpackages);
	string_list_init(&apps);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	/* read VE config */
	if ((rc = ve_config_file_read(0, veconfig, &gc, &vc, 0)))
		return rc;

	/* OS template check */
	if (vc.tmpl_type != VZ_TMPL_EZ) {
		vztt_logger(0, 0, "%s OS template is not a EZ template", \
			vc.ostemplate);
		return VZT_NOT_EZ_TEMPLATE;
	}

	/* load corresponding os template in configs directory */
	if ((rc = tmplset_load(gc.template_dir, vc.ostemplate, &vc.templates, \
			TMPLSET_LOAD_APP_LIST, &tmpl, opts_vztt->flags)))
		return rc;

	/* create & init package manager wrapper */
	if ((rc = pm_init(0, &gc, &tc, tmpl, opts_vztt, &t)))
		goto cleanup_0;

	if ((rc = pm_create_tmp_root(t)))
		goto cleanup_1;

	/* show only VZ packages */
	/* get list of vz packages from file */
	if ((rc = pm_get_installed_vzpkg(t, veprivate, &vzpackages)))
		goto cleanup_1;

	/* process templates */
	if ((rc = tmplset_check_apps(tmpl, t, \
			veprivate, &vzpackages, &apps)))
		goto cleanup_1;

	/* copy string list <ls> to string array <*a> */
	if ((rc = string_list_to_array(&apps, arr)))
		goto cleanup_1;

cleanup_1:
	/* remove temporary dir */
	pm_clean(t);
cleanup_0:
	tmplset_clean(tmpl);

	/* and clean all */
	string_list_clean(&apps);
	package_list_clean_all(&vzpackages);
	global_config_clean(&gc);
	vztt_config_clean(&tc);

	return rc;
}

/* get list of packages directories it template area, used for template cache */
int vztt2_get_cache_vzdir(
		const char *ostemplate,
		struct options_vztt *opts_vztt,
		char *cache_path,
		size_t size,
		char ***vzdir)
{
	int rc = 0;
	/* list of packages installed in VE */
	struct package_list vzpackages;
	struct global_config gc;
	struct vztt_config tc;
	struct string_list ls;
	struct package_list_el *p;
	char buf[PATH_MAX+1];
	char path[PATH_MAX+1];

	void *lockdata;

	struct Transaction *to;
	struct tmpl_set *tmpl;

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	package_list_init(&vzpackages);
	string_list_init(&ls);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		goto cleanup_0;

	/* load corresponding os template in configs directory */
	if ((rc = tmplset_load(gc.template_dir, (char *)ostemplate, \
			NULL, TMPLSET_LOAD_OS_LIST, &tmpl, \
			opts_vztt->flags)))
		goto cleanup_0;

	if (tmpl_get_cache_tar_by_type(cache_path, size,
		VZT_CACHE_TYPE_HOSTFS,
		gc.template_dir, ostemplate) == 0)
	{
		if ((rc = read_tarball(cache_path, &vzpackages)))
			goto cleanup_1;
	}
	else
	{
		rc = VZT_TMPL_NOT_CACHED;
		vztt_logger(0, 0, "%s is not cached", ostemplate);
		goto cleanup_1;
	}

	/* create & init package manager wrapper */
	if ((rc = pm_init(0, &gc, &tc, tmpl, opts_vztt, &to)))
		goto cleanup_1;

	/* lock template area on read */
	if ((rc = tmpl_lock(&gc, tmpl->base,
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_2;

	for (p = vzpackages.tqh_first; p != NULL; p = p->e.tqe_next) {
		if (!to->pm_find_pkg_area_ex(to, p->p, buf, sizeof(buf))) {
			/* VE private area is invalid, but will ignore it */
			continue;
		}
		snprintf(path, sizeof(path), "%s/%s/%s", \
			gc.template_dir, tmpl->base->basesubdir, buf);
		string_list_add(&ls, path);
	}
	tmpl_unlock(lockdata, opts_vztt->flags);

	/* copy string list <ls> to string array <*a> */
	rc = string_list_to_array(&ls, vzdir);

cleanup_2:
	/* remove temporary dir */
	pm_clean(to);
cleanup_1:
	tmplset_clean(tmpl);
cleanup_0:
	string_list_clean(&ls);
	package_list_clean(&vzpackages);
	vztt_config_clean(&tc);
	global_config_clean(&gc);

	return rc;
}


/* get list of packages directories in template area */
int vztt2_get_all_pkgs_vzdir(
		const char *ostemplate,
		struct options_vztt *opts_vztt,
		char ***vzdir)
{
	int rc = 0;
	/* list of packages installed in VE */
	struct global_config gc;
	struct vztt_config tc;
	struct string_list ls;
	char template_dir[PATH_MAX];
	char path[PATH_MAX];
	DIR *dir;
	char dirent_buf[sizeof(struct dirent) + PATH_MAX];
	struct dirent *de = (struct dirent *) dirent_buf;
	struct dirent *result;
	struct stat st;
	void *lockdata;

	struct tmpl_set *tmpl;

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	string_list_init(&ls);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		goto cleanup_0;

	/* load corresponding os template in configs directory */
	if ((rc = tmplset_load(gc.template_dir, (char *)ostemplate, \
			NULL, TMPLSET_LOAD_OS_LIST, &tmpl, \
			opts_vztt->flags)))
		goto cleanup_0;

	/* lock template area on read */
	if ((rc = tmpl_lock(&gc, tmpl->base,
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_1;

	snprintf(template_dir, sizeof(template_dir), "%s/%s", \
		gc.template_dir, tmpl->base->basesubdir);

	/* scan private directory */
	dir = opendir(template_dir);
	if (!dir)
	{
		vztt_logger(1, errno, "opendir(\"%s\") error", template_dir);
		rc = VZT_CANT_OPEN;
		goto cleanup_2;
	}

	while (1)
	{
		if ((rc = readdir_r(dir, de, &result)) != 0)
		{
			vztt_logger(1, rc, "readdir_r(\"%s\") error",
					template_dir);
			rc = VZT_CANT_READ;
			break;
		}

		if (result == NULL)
			break;

		/* Is dir? */
		snprintf(path, sizeof(path), "%s/%s", template_dir, de->d_name);
		if (lstat(path, &st))
		{
			vztt_logger(1, errno, "stat(\"%s\") error", path);
			rc = VZT_CANT_LSTAT;
			break;
		}
		if (!S_ISDIR(st.st_mode))
			continue;

		if(strcmp(de->d_name, ".") == 0)
			continue;
		if(strcmp(de->d_name, "..") == 0)
			continue;
		if (is_official_dir(de->d_name))
			continue;

		string_list_add(&ls, path);
	}
	closedir(dir);

	/* copy string list <ls> to string array <*a> */
	if (rc == 0)
		rc = string_list_to_array(&ls, vzdir);

cleanup_2:
	tmpl_unlock(lockdata, opts_vztt->flags);
cleanup_1:
	tmplset_clean(tmpl);
cleanup_0:
	string_list_clean(&ls);
	vztt_config_clean(&tc);
	global_config_clean(&gc);

	return rc;
}
