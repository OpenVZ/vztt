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
 * template & package info module
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <error.h>
#include <limits.h>
#include <getopt.h>
#include <vzctl/libvzctl.h>

#include "util.h"
#include "tmplset.h"
#include "vztt.h"
#include "lock.h"
#include "progress_messages.h"

/* get template package info */
static int get_tmpl_pkg_info(
	const char *ostemplate,
	const char *package,
	struct global_config *gc,
	struct vztt_config *tc,
	struct options_vztt *opts_vztt,
	struct pkg_info_list *pi)
{
	int rc = 0;
	void *lockdata;

	struct Transaction *to;
	struct tmpl_set *tmpl;

	/* check & update metadata */
	if ((rc = update_metadata((char *)ostemplate, gc, tc, opts_vztt)))
		return rc;

	/* load all OS and app templates */
	if ((rc = tmplset_load(gc->template_dir, (char *)ostemplate, NULL, \
			TMPLSET_LOAD_OS_LIST|TMPLSET_LOAD_APP_LIST, \
			&tmpl, opts_vztt->flags)))
		return rc;

	/* mark all - to use all available repos */
	if ((rc = tmplset_mark(tmpl, NULL, \
			TMPLSET_MARK_OS | \
			TMPLSET_MARK_OS_LIST | \
			TMPLSET_MARK_AVAIL_APP_LIST, NULL)))
		goto cleanup_0;

	/* create & init package manager wrapper */
	if ((rc = pm_init(0, gc, tc, tmpl, opts_vztt, &to)))
		goto cleanup_0;

	if ((rc = pm_create_tmp_root(to)))
		goto cleanup_1;

	/* lock template area on read */
	if ((rc = tmpl_lock(gc, tmpl->base, 
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_1;

	rc = to->pm_tmpl_get_info(to, package, pi);
	tmpl_unlock(lockdata, opts_vztt->flags);

cleanup_1:
	pm_clean(to);
cleanup_0:
	tmplset_clean(tmpl);

	return rc;
}

/* get VE package info */
static int get_ve_pkg_info(
	char *ctid,
	const char *package,
	struct global_config *gc,
	struct vztt_config *tc,
	struct options_vztt *opts_vztt,
	struct pkg_info_list *pi)
{
	int rc = 0;
	vzctl_env_status_t ve_status;
	int mounted = 0;

	struct ve_config vc;
	string_list_init(&vc.templates);

	struct Transaction *to;
	struct tmpl_set *tmpl;

	/* read OS template from VE config */
	if ((rc = check_n_load_ve_config(ctid, ENV_STATUS_EXISTS, gc, &vc)))
		goto cleanup_0;

	/* get VE status */
	if (vzctl2_get_env_status(ctid, &ve_status, ENV_STATUS_ALL))
		vztt_error(VZT_VZCTL_ERROR, 0, "Can't get status of CT %d: %s",
			ctid, vzctl2_get_last_error());

	/* load all OS and app templates */
	if ((rc = tmplset_load(gc->template_dir, vc.ostemplate, NULL, 0, \
			&tmpl, opts_vztt->flags)))
		goto cleanup_0;
	if ((rc = tmplset_mark(tmpl, NULL, TMPLSET_MARK_OS, NULL)))
		goto cleanup_1;

	/* create & init package manager wrapper */
	if ((rc = pm_init(0, gc, tc, tmpl, opts_vztt, &to)))
		goto cleanup_1;

	if ((rc = pm_set_root_dir(to, vc.ve_root)))
		goto cleanup_2;

	if (!(ve_status.mask & (ENV_STATUS_RUNNING | ENV_STATUS_MOUNTED))) {
		/* CT is not mounted or running - will mount it temporary */
		if ((rc = do_vzctl("mount", 1, 0, 0, ctid, 1, 0)))
			goto cleanup_1;
		mounted = 1;
	}
	rc = to->pm_ve_get_info(to, package, pi);
	if (mounted)
		do_vzctl("umount", 1, 0, 0, ctid, 1, 0);

cleanup_2:
	pm_clean(to);
cleanup_1:
	tmplset_clean(tmpl);
cleanup_0:
	string_list_clean(&vc.templates);

	return rc;
}

/*
 get package info
*/
int vztt_get_pkg_info(
	char *arg,
	char *package,
	struct options *opts,
	struct pkg_info ***arr)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_get_pkg_info(arg, package, opts_vztt, arr);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_get_pkg_info(
	char *arg,
	char *package,
	struct options_vztt *opts_vztt,
	struct pkg_info ***arr)
{
	int rc = 0;
	ctid_t ctid;

	struct global_config gc;
	struct vztt_config tc;

	struct pkg_info_list pi;

	progress(PROGRESS_GET_PACKAGE_INFO, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	pkg_info_list_init(&pi);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	if (is_veid(arg, ctid)) {
		rc = get_ve_pkg_info(ctid, (const char *)package, 
				&gc, &tc, opts_vztt, &pi);
	} else {
		rc = get_tmpl_pkg_info((const char *)arg, 
				(const char *)package,
				&gc, &tc, opts_vztt, &pi);
	}
	if (rc == 0)
		rc = pkg_info_list_to_array(&pi, arr);

	pkg_info_list_clean(&pi);
	global_config_clean(&gc);
	vztt_config_clean(&tc);

	progress(PROGRESS_GET_PACKAGE_INFO, 100, opts_vztt->progress_fd);

	return rc;
}


/* clean package info array */
void vztt_clean_pkg_info(struct pkg_info ***arr)
{
	int i;

	if (*arr == NULL)
		return;

	for (i = 0; (*arr)[i] != NULL; i++) {
		VZTT_FREE_STR((*arr)[i]->name);
		VZTT_FREE_STR((*arr)[i]->version);
		VZTT_FREE_STR((*arr)[i]->release);
		VZTT_FREE_STR((*arr)[i]->arch);
		VZTT_FREE_STR((*arr)[i]->summary);
		free_string_array(&((*arr)[i]->description));
		free((void *)(*arr)[i]);
	}
	free((void *)(*arr));
	*arr = NULL;
}

/*
 get application template info
*/
int vztt_get_app_tmpl_info(
	char *arg,
	char *app,
	struct options *opts,
	struct tmpl_info *info)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_get_app_tmpl_info(arg, app, opts_vztt, info);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_get_app_tmpl_info(
	char *arg,
	char *app,
	struct options_vztt *opts_vztt,
	struct tmpl_info *info)
{
	int rc = 0;
	ctid_t ctid;

	struct global_config gc;
	struct vztt_config tc;
	struct ve_config vc;

	struct tmpl_set *tmpl;

	struct string_list apps;

	progress(PROGRESS_GET_APPTEMPLATE_INFO, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	ve_config_init(&vc);
	string_list_init(&apps);

	/* skip leading dot from app name */
	if (*app == '.') app++;

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		goto cleanup_0;

	string_list_add(&apps, app);

	/* load templates */
	if (((rc = tmplset_selective_load(gc.template_dir, arg, &apps, \
		TMPLSET_LOAD_APP_LIST, &tmpl, opts_vztt))) && is_veid(arg, ctid))
	{
		/* read OS template from VE config */
		if ((rc = check_n_load_ve_config(ctid, ENV_STATUS_RUNNING, \
				&gc, &vc)))
			goto cleanup_1;

		if ((rc = tmplset_selective_load(gc.template_dir, \
					vc.ostemplate, &apps, \
					TMPLSET_LOAD_APP_LIST, &tmpl, opts_vztt)))
			goto cleanup_2;
	}
	else if (rc)
		goto cleanup_1;

	/* select only OS */
	if ((rc = tmplset_mark(tmpl, &apps, TMPLSET_MARK_AVAIL_APP_LIST, NULL)))
		goto cleanup_3;

	memset((void *)info, 0, sizeof(struct tmpl_info));

	/* get template info */
	if ((rc = tmplset_get_info(&gc, tmpl, &tc.url_map, info)))
		goto cleanup_3;

	if (tmpl->base->package_manager) {
		if (PACKAGE_MANAGER_IS_RPM_ZYPP(tmpl->base->package_manager))
			info->package_manager_type = strdup(RPM_ZYPP);
		else if (PACKAGE_MANAGER_IS_RPM(tmpl->base->package_manager))
			info->package_manager_type = strdup(RPM);
		else if (PACKAGE_MANAGER_IS_DPKG(tmpl->base->package_manager))
			info->package_manager_type = strdup(DPKG);
	}

cleanup_3:
	tmplset_clean(tmpl);
	string_list_clean(&apps);

cleanup_2:
	ve_config_clean(&vc);

cleanup_1:
	vztt_config_clean(&tc);

cleanup_0:
	global_config_clean(&gc);

	progress(PROGRESS_GET_APPTEMPLATE_INFO, 100, opts_vztt->progress_fd);

	return rc;
}

/*
 get OS template info
*/
int vztt_get_os_tmpl_info(
	char *arg,
	struct options *opts,
	struct tmpl_info *info)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_get_os_tmpl_info(arg, opts_vztt, info);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_get_os_tmpl_info(
	char *arg,
	struct options_vztt *opts_vztt,
	struct tmpl_info *info)
{
	int rc = 0;
	ctid_t ctid;

	struct global_config gc;
	struct vztt_config tc;
	struct ve_config vc;

	struct tmpl_set *tmpl;

	progress(PROGRESS_GET_OSTEMPLATE_INFO, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	ve_config_init(&vc);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		goto cleanup_0;

	/* load templates */
	if (((rc = tmplset_selective_load(gc.template_dir, arg, NULL, \
		0, &tmpl, opts_vztt))) && is_veid(arg, ctid))
	{
		/* read OS template from VE config */
		if ((rc = check_n_load_ve_config(ctid, ENV_STATUS_RUNNING, \
				&gc, &vc)))
			goto cleanup_1;

		if ((rc = tmplset_selective_load(gc.template_dir, \
					vc.ostemplate, NULL, \
					0, &tmpl, opts_vztt)))
			goto cleanup_2;
	}
	else if (rc)
		goto cleanup_1;

	/* select only OS */
	if ((rc = tmplset_mark(tmpl, NULL, TMPLSET_MARK_OS, NULL)))
		goto cleanup_3;

	memset((void *)info, 0, sizeof(struct tmpl_info));

	/* get template info */
	rc = tmplset_get_info(&gc, tmpl, &tc.url_map, info);

cleanup_3:
	tmplset_clean(tmpl);

cleanup_2:
	ve_config_clean(&vc);

cleanup_1:
	vztt_config_clean(&tc);

cleanup_0:
	global_config_clean(&gc);

	progress(PROGRESS_GET_OSTEMPLATE_INFO, 100, opts_vztt->progress_fd);

	return rc;
}

/* clean template info */
void vztt_clean_tmpl_info(struct tmpl_info *info)
{
	if (info == NULL)
		return;
	VZTT_FREE_STR(info->name);
	VZTT_FREE_STR(info->osname);
	VZTT_FREE_STR(info->osver);
	VZTT_FREE_STR(info->osarch);
	VZTT_FREE_STR(info->confdir);
	VZTT_FREE_STR(info->summary);
	free_string_array(&info->description);
	free_string_array(&info->packages0);
	free_string_array(&info->packages1);
	free_string_array(&info->packages);
	free_string_array(&info->repositories);
	free_string_array(&info->mirrorlist);
	VZTT_FREE_STR(info->package_manager);
	VZTT_FREE_STR(info->package_manager_type);
	VZTT_FREE_STR(info->distribution);
	free_string_array(&info->technologies);
	free_string_array(&info->environment);
	free_string_array(&info->upgradable_versions);
	VZTT_FREE_STR(info->cached);
}

/* get group of packages info for OS template */
static int get_tmpl_group_info(
		const char *ostemplate,
		const char *group,
		struct options_vztt *opts_vztt,
		struct group_info *info)
{
	int rc = 0;
	void *lockdata;

	struct global_config gc;
	struct vztt_config tc;

	struct Transaction *to;
	struct tmpl_set *tmpl;

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		goto cleanup_0;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		goto cleanup_0;

	/* check & update metadata */
	if ((rc = update_metadata((char *)ostemplate, &gc, &tc, opts_vztt)))
		goto cleanup_0;

	/* load all OS and app templates */
	if ((rc = tmplset_load(gc.template_dir, (char *)ostemplate, NULL, \
			TMPLSET_LOAD_OS_LIST|TMPLSET_LOAD_APP_LIST, &tmpl, \
			opts_vztt->flags)))
		goto cleanup_0;

	/* mark all - to use all available repos */
	if ((rc = tmplset_mark(tmpl, NULL, \
			TMPLSET_MARK_OS | \
			TMPLSET_MARK_OS_LIST | \
			TMPLSET_MARK_AVAIL_APP_LIST, NULL)))
		goto cleanup_1;

	/* create & init package manager wrapper */
	if ((rc = pm_init(0, &gc, &tc, tmpl, opts_vztt, &to)))
		goto cleanup_1;

	if ((rc = pm_create_tmp_root(to)))
		goto cleanup_2;

	/* lock template area on read */
	if ((rc = tmpl_lock(&gc, tmpl->base, LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_2;

	rc = to->pm_get_group_info(to, group, info);
	tmpl_unlock(lockdata, opts_vztt->flags);

cleanup_2:
	pm_clean(to);
cleanup_1:
	tmplset_clean(tmpl);
cleanup_0:
	global_config_clean(&gc);
	vztt_config_clean(&tc);

	return rc;
}

/* get group of packages info */
int vztt_get_group_info(
	const char *arg,
	const char *group,
	struct options *opts,
	struct group_info *info)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_get_group_info(arg, group, opts_vztt, info);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_get_group_info(
		const char *arg,
		const char *group,
		struct options_vztt *opts_vztt,
		struct group_info *info)
{
	int rc = 0;
	ctid_t ctid;

	progress(PROGRESS_GET_GROUP_INFO, 0, opts_vztt->progress_fd);

	if (is_veid(arg, ctid)) {
		struct global_config gc;
		struct ve_config vc;

		/* struct initialization: should be first block */
		global_config_init(&gc);
		ve_config_init(&vc);

		/* read global vz config */
		if ((rc = global_config_read(&gc, opts_vztt)))
			return rc;

		/* read OS template from VE config */
		if ((rc = check_n_load_ve_config(ctid, ENV_STATUS_EXISTS, &gc, &vc)) == 0) {
			rc = get_tmpl_group_info(vc.ostemplate, group, opts_vztt, info);
			ve_config_clean(&vc);
		}
		global_config_clean(&gc);
	} else {
		rc = get_tmpl_group_info(arg, group, opts_vztt, info);
	}

	progress(PROGRESS_GET_GROUP_INFO, 100, opts_vztt->progress_fd);

	return rc;
}

/* clean group info */
void vztt_clean_group_info(struct group_info *info)
{
	size_t i;
	if (info == NULL)
		return;
	if (info->name)
		free((void *)info->name);
	if (info->list) {
		for (i = 0; info->list[i]; i++)
			free(info->list[i]);
		free((void *)info->list);
	}
}

