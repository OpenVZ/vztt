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
 * miscellaneous vztt functions
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
#include <ploop/libploop.h>

#include "vztt.h"
#include "util.h"
#include "tmplset.h"
#include "config.h"
#include "lock.h"
#include "progress_messages.h"

/* get VE status - up2date or not */
int vztt_get_ve_status(
	const char *ctid,
	struct options *opts)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_get_ve_status(ctid, opts_vztt);
	vztt_options_free(opts_vztt);
	return rc;
}

/* get VE status - up2date or not */
int vztt2_get_ve_status(
	const char *ctid,
	struct options_vztt *opts_vztt)
{
	int rc = 0;
	int isup2date;
	void *lockdata;

	struct package_list installed;

	struct ve_config vc;
	struct global_config gc;
	struct vztt_config tc;

	struct tmpl_set *tmpl;
	struct Transaction *to;

	struct string_list ls;

	progress(PROGRESS_UP2DATE_STATUS, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	ve_config_init(&vc);
	string_list_init(&ls);
	package_list_init(&installed);

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

	/* load all os and app templates */
	if ((rc = tmplset_load(gc.template_dir, vc.ostemplate, NULL, \
			TMPLSET_LOAD_APP_LIST, &tmpl, opts_vztt->flags)))
		return rc;

	/* mark installed into VE templates only */
	if ((rc = tmplset_mark(tmpl, NULL, \
			TMPLSET_MARK_OS | \
			TMPLSET_MARK_USED_APP_LIST, NULL)))
		goto cleanup_0;

	/* create & init package manager wrapper */
	if ((rc = pm_init(0, &gc, &tc, tmpl, opts_vztt, &to)))
		goto cleanup_0;

	/* prepare transaction for Check/Update metadata */
	if ((rc = pm_create_tmp_root(to)))
		goto cleanup_1;

	/* prepare transaction for is up2date now */
	if ((rc = pm_set_root_dir(to, vc.ve_root)))
		goto cleanup_1;

	if ((rc = pm_get_installed_pkg_from_ve(to, ctid, &installed)))
		goto cleanup_1;

	/* lock template area on read */
	if ((rc = tmpl_lock(&gc, tmpl->base,
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_1;

	/* get list of installed into ve templates with repositories */
	if ((rc = tmplset_get_repos(tmpl, &ls)))
		goto cleanup_2;
	if ((rc = pm_is_up2date(to, &ls, &installed, &isup2date)))
		goto cleanup_2;

	if (isup2date) {
		/* VE is up2date */
		vztt_logger(VZTL_INFO, 0, "CT %s is up2date", ctid);
		rc = 0;
	}
	else {
		vztt_logger(VZTL_EINFO, 0, "CT %s is not up2date", ctid);
		rc = VZT_NOT_UP2DATE;
	}

cleanup_2:
	tmpl_unlock(lockdata, opts_vztt->flags);
cleanup_1:
	pm_clean(to);
cleanup_0:
	tmplset_clean(tmpl);

	package_list_clean(&installed);
	string_list_clean(&ls);
	global_config_clean(&gc);
	vztt_config_clean(&tc);
	ve_config_clean(&vc);

	progress(PROGRESS_UP2DATE_STATUS, 100, opts_vztt->progress_fd);

	return rc;
}

/* get OS template cache status - up2date or not */
int vztt_get_cache_status(
	char *ostemplate,
	struct options *opts)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_get_cache_status(ostemplate, opts_vztt);
	vztt_options_free(opts_vztt);
	return rc;
}

static int is_cache_tarball_up2date(
	struct global_config *gc,
	struct vztt_config *tc,
	struct tmpl_set *tmpl,
	struct options_vztt *opts_vztt,
	char *tarball,
	int *isup2date)
{
	int rc = 0;
	void *lockdata;

	/* list of packages installed in VE */
	struct package_list installed;
	struct Transaction *to;
	struct string_list ls;


	string_list_init(&ls);
	package_list_init(&installed);

	/* create & init package manager wrapper */
	if ((rc = pm_init(0, gc, tc, tmpl, opts_vztt, &to)))
		return rc;

	/* prepare transaction for Check/Update metadata */
	if ((rc = pm_create_tmp_root(to)))
		goto cleanup_0;

	if ((rc = read_tarball(tarball, &installed)))
		goto cleanup_0;

	/* lock template area on read */
	if ((rc = tmpl_lock(gc, tmpl->base,
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_0;

	if ((rc = tmplset_get_repos(tmpl, &ls)))
		goto cleanup_1;
	if ((rc = pm_is_up2date(to, &ls, &installed, isup2date)))
		goto cleanup_1;

	if (*isup2date)
		vztt_logger(VZTL_INFO, 0, "Cache %s is up to date", tarball);
	else
		vztt_logger(VZTL_EINFO, 0, "Cache %s is not up to date", tarball);

cleanup_1:
	tmpl_unlock(lockdata, opts_vztt->flags);
cleanup_0:
	pm_clean(to);
	package_list_clean(&installed);
	string_list_clean(&ls);

	return rc;
}

int vztt2_get_cache_status(
	char *ostemplate,
	struct options_vztt *opts_vztt)
{
	int rc = 0;
	int isup2date = 0;
	int isup2date_all = 1;
	int cache_found = 0;
	char buf[PATH_MAX+1];

	struct global_config gc;
	struct vztt_config tc;
	struct tmpl_set *tmpl;

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);

	progress(PROGRESS_GET_CACHE_STATUS, 0, opts_vztt->progress_fd);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		goto cleanup_0;

	/* check & update metadata */
	if ((rc = update_metadata(ostemplate, &gc, &tc, opts_vztt)))
		goto cleanup_1;

	/* load all templates */
	if ((rc = tmplset_load(gc.template_dir, ostemplate, NULL, 0,
			&tmpl, opts_vztt->flags)))
		goto cleanup_1;

	/* to use only base and os repos */
	if ((rc = tmplset_mark(tmpl, NULL, TMPLSET_MARK_OS, NULL)))
		goto cleanup_2;

	/* vefstype "all" case */
	if (gc.veformat == 0)
	{
		/* Check for ploopv2 first */
		if (tmpl_get_cache_tar_by_type(buf, sizeof(buf),
			VZT_CACHE_TYPE_SIMFS | VZT_CACHE_TYPE_PLOOP_V2,
			gc.template_dir, ostemplate) == 0 &&
			ploop_is_large_disk_supported())
		{
			cache_found = 1;

			if ((rc = is_cache_tarball_up2date(&gc, &tc, tmpl, opts_vztt,
				buf, &isup2date)))
				goto cleanup_2;

			if (!isup2date)
				isup2date_all = 0;
		}

		/* Then check for ploop */
		if (tmpl_get_cache_tar_by_type(buf, sizeof(buf),
			VZT_CACHE_TYPE_SIMFS | VZT_CACHE_TYPE_PLOOP,
			gc.template_dir, ostemplate) == 0)
		{
			cache_found = 1;

			if ((rc = is_cache_tarball_up2date(&gc, &tc, tmpl, opts_vztt,
				buf, &isup2date)))
				goto cleanup_2;

			if (!isup2date)
				isup2date_all = 0;
		}

		/* Then check for VZFS */
		if (tmpl_get_cache_tar_by_type(buf, sizeof(buf),
			VZT_CACHE_TYPE_HOSTFS,
			gc.template_dir, ostemplate) == 0)
		{
			cache_found = 1;

			if ((rc = is_cache_tarball_up2date(&gc, &tc, tmpl, opts_vztt,
				buf, &isup2date)))
				goto cleanup_2;

			if (!isup2date)
				isup2date_all = 0;
		}

		/* Latest check for SIMFS */
		if (tmpl_get_cache_tar_by_type(buf, sizeof(buf),
			VZT_CACHE_TYPE_SIMFS | VZT_CACHE_TYPE_HOSTFS,
			gc.template_dir, ostemplate) == 0)
		{
			cache_found = 1;

			if ((rc = is_cache_tarball_up2date(&gc, &tc, tmpl, opts_vztt,
				buf, &isup2date)))
				goto cleanup_2;
		}
	}
	/* Check status only for given vefstype */
	else
	{
		if (tmpl_get_cache_tar_by_type(buf, sizeof(buf),
			get_cache_type(&gc), gc.template_dir, ostemplate) == 0)
		{
			cache_found = 1;

			if ((rc = is_cache_tarball_up2date(&gc, &tc, tmpl, opts_vztt,
				buf, &isup2date)))
				goto cleanup_2;
		}
	}

	if (!cache_found)
	{
		rc = VZT_TMPL_NOT_CACHED;
		vztt_logger(0, 0, "%s is not cached", ostemplate);
		goto cleanup_2;
	}

	if (isup2date && isup2date_all) {
		/* VE is up2date */
		vztt_logger(VZTL_INFO, 0, "Caches of %s are up to date", ostemplate);
		rc = 0;
	} else {
		vztt_logger(VZTL_EINFO, 0, "Caches of %s are not up to date",
				ostemplate);
		rc = VZT_NOT_UP2DATE;
	}

cleanup_2:
	tmplset_clean(tmpl);
cleanup_1:
	vztt_config_clean(&tc);
cleanup_0:
	global_config_clean(&gc);

	progress(PROGRESS_GET_CACHE_STATUS, 100, opts_vztt->progress_fd);

	return rc;
}

/*
 Repair template area:
 download installed in VE private area packages
 and unpack content in template area
*/
int vztt_repair(
	const char *ve_private,
	const char *veconf,
	struct options *opts)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_repair(ve_private, veconf, opts_vztt);
	vztt_options_free(opts_vztt);
	return rc;
}

// Repair used only for VZFS
int vztt2_repair(
	const char *ve_private,
	const char *veconf,
	struct options_vztt *opts_vztt)
{
	progress(PROGRESS_REPAIR, 0, opts_vztt->progress_fd);

	progress(PROGRESS_REPAIR, 100, opts_vztt->progress_fd);

	return 0;
}

/*
 Link VE: convert regular file to magic in VE private area
*/
int vztt_link(
	const char *ctid,
	struct options *opts)
{
	return VZT_UNSUPPORTED_COMMAND;
}

int vztt2_link(
	const char *ctid,
	struct options_vztt *opts_vztt)
{
	return VZT_UNSUPPORTED_COMMAND;
}

/*
 To read VE package manager database and correct file vzpackages & app templates list
*/
int vztt_sync_vzpackages(
	const char *ctid,
	struct options *opts)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_sync_vzpackages(ctid, opts_vztt);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_sync_vzpackages(
	const char *ctid,
	struct options_vztt *opts_vztt)
{
	int rc = 0;
	void *velockdata;

	/* list of vz packages, installed in VE */
	struct package_list vzpackages;
	/* list of application templates */
	struct string_list apps;

	struct global_config gc;
	struct vztt_config tc;
	struct ve_config vc;

	struct Transaction *to;
	struct tmpl_set *tmpl;

	progress(PROGRESS_SYNC_PACKAGES, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	ve_config_init(&vc);
	package_list_init(&vzpackages);
	string_list_init(&apps);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	if ((rc = check_n_load_ve_config(ctid, ENV_STATUS_EXISTS, &gc, &vc)))
		return rc;

	if ((rc = tmplset_load(gc.template_dir, vc.ostemplate, NULL, \
			TMPLSET_LOAD_APP_LIST, &tmpl, opts_vztt->flags)))
		return rc;

	if ((rc = tmplset_mark(tmpl, NULL, TMPLSET_MARK_OS, NULL)))
		goto cleanup_0;

	/* create & init package manager wrapper */
	if ((rc = pm_init(0, &gc, &tc, tmpl, opts_vztt, &to)))
		goto cleanup_0;

	if ((rc = pm_set_root_dir(to, vc.ve_root)))
		goto cleanup_1;

	if (vc.veformat == VZ_T_VZFS0) {
		rc = to->pm_get_install_pkg(to, &vzpackages);
	} else if (opts_vztt->flags & OPT_VZTT_SKIP_DB) {
		rc = pm_get_installed_vzpkg(to, vc.ve_private, &vzpackages);
	} else {
		rc = pm_get_installed_vzpkg2(to, ctid, vc.ve_private, &vzpackages);
	}
	if (rc)
		goto cleanup_1;

	/* lock VE */
	if ((rc = lock_ve(ctid, opts_vztt->flags, &velockdata)))
		goto cleanup_1;

	if ((rc = save_vzpackages(vc.ve_private, &vzpackages)))
		goto cleanup_2;

	/* if application template autodetection is off - do not change
	   CT app templates list */
	if (tc.apptmpl_autodetect) {
		/* process templates */
		tmplset_check_apps(tmpl, to, vc.ve_private, &vzpackages, &apps);

		/* save new app templates set in VE config */
		if ((rc = ve_config1_save(vc.config, &apps)))
			goto cleanup_2;
	}

cleanup_2:
	unlock_ve(ctid, velockdata, opts_vztt->flags);
cleanup_1:
	/* remove temporary dir */
	pm_clean(to);
cleanup_0:
	tmplset_clean(tmpl);

	package_list_clean(&vzpackages);
	string_list_clean(&apps);
	global_config_clean(&gc);
	vztt_config_clean(&tc);
	ve_config_clean(&vc);

	progress(PROGRESS_SYNC_PACKAGES, 100, opts_vztt->progress_fd);

	return rc;
}

/*
 Fetch packages for all os and app templates
*/
int vztt_fetch(
	char *ostemplate,
	struct options *opts)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_fetch(ostemplate, opts_vztt);
	vztt_options_free(opts_vztt);
	return rc;
}

/*
 Fetch packages for all os and app templates
*/
int vztt2_fetch(
	char *ostemplate,
	struct options_vztt *opts_vztt)
{
	int rc = 0;
	int load_mask = 0, mark_mask = 0;
	void *lockdata;

	/* list of transaction args */
	struct string_list args;

	struct global_config gc;
	struct vztt_config tc;

	struct Transaction *to;
	struct tmpl_set *tmpl;

	progress(PROGRESS_FETCH_PACKAGES, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	string_list_init(&args);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	/* check & update metadata */
	if ((rc = update_metadata(ostemplate, &gc, &tc, opts_vztt)))
		return rc;

	/* select only OS, only app or both */
	if (opts_vztt->templates & OPT_TMPL_OS) {
		mark_mask |= TMPLSET_MARK_OS | TMPLSET_MARK_OS_LIST;
		load_mask |= TMPLSET_LOAD_OS_LIST;
	}
	if (opts_vztt->templates & 	OPT_TMPL_APP) {
		mark_mask |= TMPLSET_MARK_AVAIL_APP_LIST;
		load_mask |= TMPLSET_LOAD_APP_LIST;
	}
	/* load templates */
	if ((rc = tmplset_load(gc.template_dir, ostemplate, NULL, load_mask,\
			&tmpl, opts_vztt->flags)))
		return rc;

	if ((rc = tmplset_mark(tmpl, NULL, mark_mask, NULL)))
		goto cleanup_0;

	if ((rc = tmplset_get_marked_pkgs(tmpl, &args)))
		goto cleanup_0;

	/* create & init package manager wrapper */
	if ((rc = pm_init(0, &gc, &tc, tmpl, opts_vztt, &to)))
		goto cleanup_0;

	/* prepare transaction for packages downloading */
	if ((rc = pm_create_tmp_root(to)))
		goto cleanup_1;

	/* lock template area on read */
	if ((rc = tmpl_lock(&gc, tmpl->base,
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_1;
	/* start of processing */
	rc = to->pm_action(to, VZPKG_FETCH, &args);
	tmpl_unlock(lockdata, opts_vztt->flags);

cleanup_1:
	/* remove temporary dir */
	pm_clean(to);
cleanup_0:
	tmplset_clean(tmpl);

	string_list_clean(&args);
	global_config_clean(&gc);
	vztt_config_clean(&tc);

	progress(PROGRESS_FETCH_PACKAGES, 100, opts_vztt->progress_fd);

	return rc;
}

/*
 Fetch packages for one template
*/
static int fetch_one_template(
	char *template,
	struct global_config *gc,
	struct vztt_config *tc,
	struct tmpl_set *tmpl,
	struct options_vztt *opts_vztt)
{
	int rc = 0;
	struct Transaction *to;

	/* list for one template */
	struct string_list ls;
	/* list of transaction packages */
	struct string_list packages;
	char progress_stage[PATH_MAX];

	snprintf(progress_stage, sizeof(progress_stage),
		PROGRESS_FETCH_TEMPLATE, template);
	progress(progress_stage, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	string_list_init(&ls);
	string_list_init(&packages);

	/* select only OS, only app or both */
	string_list_add(&ls, template);
	if ((rc = tmplset_mark(tmpl, &ls,
			TMPLSET_MARK_OS | \
			TMPLSET_MARK_OS_LIST | \
			TMPLSET_MARK_AVAIL_APP_LIST,
			NULL)))
		goto cleanup_0;

	if ((rc = tmplset_get_marked_pkgs(tmpl, &packages)))
		goto cleanup_1;

	/* create & init package manager wrapper */
	if ((rc = pm_init(0, gc, tc, tmpl, opts_vztt, &to)))
		goto cleanup_1;

	/* prepare transaction for packages downloading */
	if ((rc = pm_create_tmp_root(to)))
		goto cleanup_2;

	/* start of processing */
	rc = to->pm_action(to, VZPKG_FETCH, &packages);

cleanup_2:
	pm_clean(to);
cleanup_1:
	tmplset_unmark_all(tmpl);
cleanup_0:
	string_list_clean(&packages);
	string_list_clean(&ls);

	progress(progress_stage, 100, opts_vztt->progress_fd);

	return rc;
}

/*
 Fetch packages for os and app templates separately, per-template
*/
int vztt_fetch_separately(char *ostemplate,
		struct options *opts)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_fetch_separately(ostemplate, opts_vztt);
	vztt_options_free(opts_vztt);
	return rc;
}

/*
 Fetch packages for os and app templates separately, per-template
*/
int vztt2_fetch_separately(char *ostemplate,
		struct options_vztt *opts_vztt)
{
	int rc;
	struct global_config gc;
	struct vztt_config tc;
	struct tmpl_set *tmpl;
	struct os_tmpl_list_el *o;
	struct app_tmpl_list_el *a;
	void *lockdata;

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		goto cleanup_0;

	/* check & update metadata */
	if ((rc = update_metadata(ostemplate, &gc, &tc, opts_vztt)))
		goto cleanup_0;

	/* load all OS and app templates */
	if ((rc = tmplset_load(gc.template_dir, ostemplate, NULL,\
			TMPLSET_LOAD_OS_LIST|TMPLSET_LOAD_APP_LIST, \
			&tmpl, opts_vztt->flags)))
		goto cleanup_0;

	/* lock template area on read */
	if ((rc = tmpl_lock(&gc, tmpl->base,
			LOCK_READ, opts_vztt->flags, &lockdata)))
		goto cleanup_1;
	if (opts_vztt->templates & OPT_TMPL_OS) {
		vztt_logger(2, 0, "Fetch %s base ostemplate", tmpl->base->name);
		if ((rc = fetch_one_template(tmpl->base->name,
				&gc, &tc, tmpl, opts_vztt)) &&
				!(opts_vztt->flags & OPT_VZTT_FORCE))
			goto cleanup_2;

		for (o = tmpl->oses.tqh_first; o != NULL; o = o->e.tqe_next) {
			vztt_logger(2, 0, "Fetch %s ostemplate", o->tmpl->name);
			if ((rc = fetch_one_template(o->tmpl->name,
					&gc, &tc, tmpl, opts_vztt))
					&& !(opts_vztt->flags & OPT_VZTT_FORCE))
				goto cleanup_2;

		}
	}
	if (opts_vztt->templates & OPT_TMPL_APP) {
		for (a = tmpl->avail_apps.tqh_first; a != NULL;
				a = a->e.tqe_next){
			vztt_logger(2, 0, "Fetch %s apptemplate", a->tmpl->name);
			if ((rc = fetch_one_template(a->tmpl->name,
					&gc, &tc, tmpl, opts_vztt))
					&& !(opts_vztt->flags & OPT_VZTT_FORCE))
				goto cleanup_2;
		}
	}

cleanup_2:
	tmpl_unlock(lockdata, opts_vztt->flags);
cleanup_1:
	tmplset_clean(tmpl);
cleanup_0:
	global_config_clean(&gc);
	vztt_config_clean(&tc);
	return 0;
}



/*
 Get all installed base OS template
*/
int vztt_get_all_base(char ***arr)
{
	int rc = 0;

	struct string_list ls;
	struct global_config gc;

	/* struct initialization: should be first block */
	global_config_init(&gc);
	string_list_init(&ls);

	if ((rc = global_config_read(&gc, 0)))
		return rc;

	if ((rc = tmplset_get_all_base(gc.template_dir, &ls)))
		return rc;

	rc = string_list_to_array(&ls, arr);

	string_list_clean(&ls);
	global_config_clean(&gc);
	return rc;
}


/* return 1 if <name> is standard template name */
int vztt_is_std_template(char *name)
{
	char *p;
	int rc = 0;
	char path[PATH_MAX+1];
	struct stat st;
	struct global_config gc;

	/* struct initialization: should be first block */
	global_config_init(&gc);

	if ((rc = global_config_read(&gc, 0)))
		return rc;

	/* find template version and cut off */
	if ((p = strchr(name, '/')))
		*p = '\0';

	snprintf(path, sizeof(path), "%s/%s/conf", gc.template_dir, name);
	if (stat(path, &st) == 0)
		rc = S_ISDIR(st.st_mode);
	if (p)
		*p = '/';

	global_config_clean(&gc);
	return rc;
}

/* get ve os template */
int vztt_get_ve_ostemplate(const char *ctid, char **ostemplate, int *tmpl_type)
{
	int rc;

	/* check VE state */
	if ((rc = check_ve_state(ctid, ENV_STATUS_EXISTS)))
		return rc;

	if ((rc = ve_config_ostemplate_read(ctid, ostemplate, tmpl_type)))
		return rc;

	if (*ostemplate == NULL)
		return vztt_error(VZT_VE_OSTEMPLATE_NOTSET, 0,
			"OSTEMPLATE is not set for CT %s", ctid);

	return 0;
}

/* does <rpm> provides ez template */
int vztt_is_ez_rpm(const char *rpm)
{
	int rc = 0;
	FILE *fd;
	char buf[PATH_MAX+1];
	char *p, *tdir = TEMPLATE_DIR, *pkgs = "/packages";

	/* is it rpm with ez template? */
	snprintf(buf, sizeof(buf), "rpm -qpl %s", rpm);
	if ((fd = popen(buf, "r")) == NULL)
		return 1;

	/* mask are : TEMPLATE_DIR/<osname>/<osver>/<osarch>/config/... */
	while(fgets(buf, sizeof(buf), fd)) {
		if ((p = strchr(buf, '\n')))
			*p = '\0';
		if (strncmp(buf, tdir, strlen(tdir)))
			continue;
		if (strstr(buf, "/config/") == NULL)
			continue;
		if (strcmp(buf+strlen(buf)-strlen(pkgs), pkgs))
			continue;
		rc = 1;
	}
	pclose(fd);

	return rc;
}

/* check ez rpm content, check this template on node, get template name */
static int check_ez_rpm(
		const char *rpm,
		int vztt,
		struct string_list *ls,
		struct string_list *existed)
{
	int rc = 0;
	int is_ez;
	FILE *fd;
	char cmd[PATH_MAX+100];
	char buf[PATH_MAX+1];
	char name[PATH_MAX+1];
	char *p, *arch;
	char *tdir = TEMPLATE_DIR;
	char *pkgs = "/packages";
	char *c_os = "config/os/";
	char *c_app = "config/app/";
	char *s_cf = "/config/";
	char *basename, *extname, *appname;
	int installed = 0;

	if (access(rpm, F_OK)) {
		vztt_logger(2, 0, "File %s not found", rpm);
		return VZT_FILE_NFOUND;
	}

	/* is it rpm with ez template? */
	snprintf(cmd, sizeof(cmd), "rpm -qpl %s", rpm);
	if ((fd = popen(cmd, "r")) == NULL) {
		vztt_logger(0, 0, "popen(%s)", cmd);
		return VZT_SYSTEM;
	}

	/* mask are : TEMPLATE_DIR/<osname>/<osver>/<osarch>/config/... */
	is_ez = 0;
	buf[0] = '\0';
	while(fgets(buf, sizeof(buf), fd)) {
		if ((p = strchr(buf, '\n')))
			*p = '\0';
		if (strncmp(buf, tdir, strlen(tdir)))
			continue;
		if (strstr(buf, s_cf) == NULL)
			continue;
		if (strcmp(buf+strlen(buf)-strlen(pkgs), pkgs))
			continue;
		is_ez = 1;
		break;
	}
	pclose(fd);
	if (!is_ez) {
		vztt_logger(0, 0, "%s is not EZ template", rpm);
		return VZT_NOT_EZ_TEMPLATE;
	}

	/* find 'packages' file */
	if (access(buf, F_OK) == 0) {
		/* template already installed */
		installed = 1;
	}

	/* ok - it is ez template. try to get name */
        buf[strlen(buf)-strlen(pkgs)] = '\0';
	p = buf + strlen(tdir);
	while(*p == '/') p++;
	basename = p;
	p = strstr(buf, s_cf);
	*(p++) = '\0';
	/* get architecture */
	if ((arch = strrchr(basename, '/')) == NULL) {
		vztt_logger(0, 0, "%s is not EZ template", rpm);
		return VZT_NOT_EZ_TEMPLATE;
	}
	/* check arch */
	if ((rc = tmplset_check_arch(++arch)) && !(vztt & OPT_VZTT_FORCE))
		return rc;

	if (strncmp(p, c_os, strlen(c_os)) == 0) {
		strncpy(name, basename, sizeof(name));
		extname = p + strlen(c_os);
		if (strcmp(extname, DEFSETNAME)) {
			strncat(name, "-", sizeof(name)-strlen(name)-1);
			strncat(name, extname, sizeof(name)-strlen(name)-1);
		}
	} else if (strncmp(p, c_app, strlen(c_app)) == 0) {
		appname = p + strlen(c_app);
		if (strcmp(appname+strlen(appname)-strlen(DEFSETNAME)-1, \
				"/" DEFSETNAME) == 0)
			appname[strlen(appname)-strlen(DEFSETNAME)-1] = '\0';
		strncpy(name, basename, sizeof(name));
		strncat(name, " ", sizeof(name)-strlen(name)-1);
		strncat(name, appname, sizeof(name)-strlen(name)-1);
	} else {
		vztt_logger(0, 0, "%s is not EZ template", rpm);
		return VZT_NOT_EZ_TEMPLATE;
	}
	/* replace '/' to '-' in name */
	for (p = strchr(name, '/'); p; p = strchr(p, '/'))
		*p = '-';

	string_list_add(ls, name);
	if (installed)
		string_list_add(existed, name);

	return 0;
}

/* Check throw vzup2date is installed ez-template or not */
static int check_vzup2date_eztmpl(
		struct string_list *vzup2date_inst,
		struct string_list *ls,
		struct options_vztt *opts_vztt,
		char *rpm)
{
	char name[PATH_MAX+1];
	char name_print[PATH_MAX+1];

	if (opts_vztt->for_obj) {
		snprintf(name, PATH_MAX+1, "%s-%s-ez", rpm,
		    opts_vztt->for_obj);
		snprintf(name_print, PATH_MAX+1, "%s %s",
		    opts_vztt->for_obj, rpm);
	} else {
		snprintf(name, PATH_MAX+1, "%s-ez", rpm);
		snprintf(name_print, PATH_MAX+1, "%s", rpm);
	}

	string_list_add(vzup2date_inst, name);
	string_list_add(ls, name_print);
	return (strlen(name) + 1);
}

/* install/update template as rpm package <rpm> on HN */
/* TODO: get ostemplate name from rpm and lock ostemplate */
static int do_template(
		int action,
		char **rpms,
		size_t sz,
		struct options_vztt *opts_vztt,
		char ***arr)
{
	int rc = 0;
	size_t i, len, len_update, a = 0, szi = sz;
	char *cmd = NULL;
	char *cmd_update = NULL;
	char *update_install = " install -y";
	char *update_update = " update -y";
	char *rpmu = RPMBIN " -U";
	char *force = " --force --nodeps";
	char *quiet = " --quiet", *verbose = " -hv", *test = " --test",
		*testyum = " --assumeno";
	struct string_list ls;
	struct string_list existed;
	struct string_list packages_list;
	struct string_list_el *s;
	char *rpmsi[szi];
	char progress_stage[PATH_MAX];

	if (action == VZPKG_INSTALL)
		snprintf(progress_stage, sizeof(progress_stage),
			PROGRESS_INSTALL_TEMPLATE);
	else
		snprintf(progress_stage, sizeof(progress_stage),
			PROGRESS_UPDATE_TEMPLATE);
	progress(progress_stage, 0, opts_vztt->progress_fd);

	string_list_init(&ls);
	string_list_init(&existed);
	string_list_init(&packages_list);

	len_update = strlen(YUM) + strlen(update_install) + 1;
	if (opts_vztt->flags & OPT_VZTT_FORCE)
		len = strlen(rpmu) + strlen(force) + 1;
	else
		len = len_update;
	if (opts_vztt->flags & OPT_VZTT_QUIET) {
		len += strlen(quiet);
		len_update += strlen(quiet);
	} else {
		len += strlen(verbose);
	}
	if (opts_vztt->flags & OPT_VZTT_TEST) {
		len += (opts_vztt->flags & OPT_VZTT_FORCE ?
			strlen(test) : strlen(testyum));
		len_update += strlen(testyum);
	}
	for (i = 0; rpms[i] && (i < sz); i++) {
		if ((rc = check_ez_rpm(rpms[i], opts_vztt->flags, &ls, &existed))) {
			if (rc == VZT_FILE_NFOUND &&
				(opts_vztt->flags & OPT_VZTT_USE_VZUP2DATE)) {
				len_update += check_vzup2date_eztmpl(&packages_list,
					&ls, opts_vztt, rpms[i]);
				szi--;
			} else {
				goto cleanup;
			}
		} else {
			rpmsi[a] = rpms[i];
			len += strlen(rpms[i]) + 1;
			a++;
		}
	}

	#pragma GCC diagnostic ignored "-Waddress"
	if ((action == VZPKG_INSTALL) && (!string_list_empty(&existed))) {
		vztt_logger(0, 0, "The next template(s) already installed:");
		string_list_for_each(&existed, s)
			vztt_logger(-1, 0, "\t%s", s->s);
		if (!(opts_vztt->flags & OPT_VZTT_FORCE)) {
			rc = VZT_TMPL_INSTALLED;
			goto cleanup;
		}
	}

	if (!string_list_empty(&packages_list)) {
		if ((cmd_update = (char *)malloc(len_update + 1)) == NULL) {
			vztt_logger(0, errno, "Cannot alloc memory");
			rc = VZT_CANT_ALLOC_MEM;
			goto cleanup;
		}

		strncpy(cmd_update, YUM, len_update);
		strncat(cmd_update,
			action == VZPKG_INSTALL?
				update_install:
				update_update,
			len_update - strlen(cmd_update) - 1);
		string_list_for_each(&packages_list, s) {
			strncat(cmd_update, " ", len_update -
                                strlen(cmd_update) - 1);
			strncat(cmd_update, s->s, len_update -
				strlen(cmd_update) - 1);
		}

		if (opts_vztt->flags & OPT_VZTT_QUIET)
			strncat(cmd_update, quiet, len_update -
                                strlen(cmd_update) - 1);

		if (opts_vztt->flags & OPT_VZTT_TEST)
			strncat(cmd_update, testyum, len_update -
				strlen(cmd_update)-1);

		vztt_logger(1, 0, "RPM package(s) is (are) not found, running %s", cmd_update);

		if ((rc = execv_cmd(cmd_update, (opts_vztt->flags & OPT_VZTT_QUIET), 1)))
			goto cleanup;
	}

	if ((cmd = (char *)malloc(len + 1)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup;
	}

	if (opts_vztt->flags & OPT_VZTT_FORCE) {
		strncpy(cmd, rpmu, len);
		strncat(cmd, force, len-strlen(cmd)-1);
		if ((opts_vztt->flags & OPT_VZTT_QUIET) == 0)
			strncat(cmd, verbose, len-strlen(cmd)-1);
		if (opts_vztt->flags & OPT_VZTT_TEST)
			strncat(cmd, test, len-strlen(cmd)-1);
	} else {
		strncpy(cmd, YUM, len);
		strncat(cmd, update_install, len-strlen(cmd)-1);
		if (opts_vztt->flags & OPT_VZTT_TEST)
			strncat(cmd, testyum, len-strlen(cmd)-1);
	}

	if (opts_vztt->flags & OPT_VZTT_QUIET)
		strncat(cmd, quiet, len-strlen(cmd)-1);
	for (i = 0; rpmsi[i] && (i < szi); i++) {
		strncat(cmd, " ", len-strlen(cmd)-1);
		strncat(cmd, rpmsi[i], len-strlen(cmd)-1);
	}

	if (szi)
		if ((rc = execv_cmd(cmd, (opts_vztt->flags & OPT_VZTT_QUIET), 1)))
			goto cleanup;

	/* copy string list <ls> to string array <*a> */
	if (arr)
		rc = string_list_to_array(&ls, arr);

cleanup:
	if (cmd)
		free((void *)cmd);

	if (cmd_update)
		free((void *)cmd_update);

	string_list_clean(&ls);
	string_list_clean(&existed);
	string_list_clean(&packages_list);

	progress(progress_stage, 100, opts_vztt->progress_fd);
	return rc;
}

/* install template as rpm package <rpm> on HN */
int vztt_install_template(
		char **rpms,
		size_t sz,
		struct options *opts,
		char ***arr)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = do_template(VZPKG_INSTALL, rpms, sz, opts_vztt, arr);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_install_template(
		char **rpms,
		size_t sz,
		struct options_vztt *opts_vztt,
		char ***arr)
{
	return do_template(VZPKG_INSTALL, rpms, sz, opts_vztt, arr);
}

/* update template as rpm package <rpm> on HN */
int vztt_update_template(
		char **rpms,
		size_t sz,
		struct options *opts,
		char ***arr)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = do_template(VZPKG_UPDATE, rpms, sz, opts_vztt, arr);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_update_template(
		char **rpms,
		size_t sz,
		struct options_vztt *opts_vztt,
		char ***arr)
{
	return do_template(VZPKG_UPDATE, rpms, sz, opts_vztt, arr);
}

/* check OSTEMPLATE on available shared private area */
static int check_shared_private_ostemplate(
	struct global_config *gc,
	struct tmpl_set *tmpl,
	struct options_vztt *opts_vztt)
{
	char path[PATH_MAX+1];
	int rc = 0;
	char **plist;
	int i;
	struct ve_config vc;
	struct os_tmpl_list_el *o;

	struct string_list names;
	struct string_list used;
	struct string_list_el *s;

	ve_config_init(&vc);
	string_list_init(&names);
	string_list_init(&used);

	vztt_logger(2, 0, "Template area resides on the shared partition. "
		"Will check all available private areas.");

	/* get OS template name(s) list for search */
	if ((rc = string_list_add(&names, tmpl->os->name)))
		goto cleanup_0;
	if (tmpl->os == (struct os_tmpl *)tmpl->base) {
		/* ostemplate is base os template */
		for (o = tmpl->oses.tqh_first; o != NULL; o = o->e.tqe_next) {
			if ((rc = string_list_add(&names, o->tmpl->name)))
				goto cleanup_0;
		}
	}

	/* get private areas list */
	if ((plist = vzctl2_scan_private()) == NULL) {
		rc = vztt_error(VZT_VZCTL_ERROR, 0,
			"vzctl2_scan_private() error: %s",
			vzctl2_get_last_error());
		goto cleanup_0;
	}

	for (i = 0; plist[i]; i++) {
		ve_config_clean(&vc);
		/* read config file */
		vztt_logger(2, 0, "Check private %s", plist[i]);
		snprintf(path, sizeof(path), "%s/" VE_CONFIG, plist[i]);
		if ((rc = ve_config_file_read(0, path, gc, &vc, 1))) {
			if (opts_vztt->flags & OPT_VZTT_FORCE)
				continue;
			else
				break;
		}
		/* compare OS template */
		if (vc.tmpl_type != VZ_TMPL_EZ)
			continue;
		if (string_list_find(&names, vc.ostemplate) == NULL)
			continue;
		if ((rc = string_list_add(&used, plist[i])))
			break;
	}
	if (rc == 0 && !string_list_empty(&used)) {
		vztt_logger(0, 0, "The next CT private(s) based on %s "
			"EZ OS template:", tmpl->os->name);
		string_list_for_each(&used, s)
			vztt_logger(VZTL_EINFO, 0, "%s", s->s);
		rc = VZT_TMPL_INSTALLED;
	}

	for (i = 0; plist[i]; i++)
		free((void *)plist[i]);
	free((void *)plist);
	ve_config_clean(&vc);
cleanup_0:
	string_list_clean(&used);
	string_list_clean(&names);

	return rc;
}

/* check that app and os template rpms are the same */
static int check_app_rpm_equal_os(struct tmpl_set *tmpl, struct tmpl *atmpl) {
	char *os_rpm = NULL;
	char *rpm = NULL;
	int rc = 0;

	if (tmpl_get_rpm((struct tmpl *)tmpl->os, &os_rpm) == 0 &&
		tmpl_get_rpm(atmpl, &rpm) == 0 &&
		strcmp(os_rpm, rpm) == 0)
		rc = 1;

	VZTT_FREE_STR(rpm)
	VZTT_FREE_STR(os_rpm)
	return rc;
}

/* is <ostemplate> is needs by anybody */
static int check_ostemplate_toremove(struct tmpl_set *tmpl)
{
	int rc = 0;
	struct string_list ve_list;
	struct string_list_el *v;
	struct os_tmpl_list_el *o;
	struct app_tmpl_list_el *a;

	string_list_init(&ve_list);

	/* Drop app templates installed in rpm together with os */
	app_tmpl_list_for_each(&tmpl->avail_apps, a)
		if (check_app_rpm_equal_os(tmpl, (struct tmpl *)a->tmpl))
			app_tmpl_list_remove(&tmpl->avail_apps, a);

	if (tmpl->os == (struct os_tmpl *)tmpl->base) {
		/* ostemplate is base os template */
		if (!os_tmpl_list_empty(&tmpl->oses)) {
			vztt_logger(0, 0, "There are non-default os template(s):");
			for (o = tmpl->oses.tqh_first; o != NULL; \
					o = o->e.tqe_next)
				vztt_logger(VZTL_EINFO, 0, "%s", o->tmpl->name);
			rc = VZT_TMPL_HAS_EXTRA;
		} else if (!app_tmpl_list_empty(&tmpl->avail_apps)) {
			vztt_logger(0, 0, "There are application template(s):");
			for (a = tmpl->avail_apps.tqh_first; a != NULL; \
					a = a->e.tqe_next)
				vztt_logger(VZTL_EINFO, 0, "%s %s", tmpl->base->name, \
					a->tmpl->name);
			rc = VZT_TMPL_HAS_APPS;
		} else
			rc = tmplset_get_velist_for_base(tmpl, &ve_list);
	} else {
		/* ostemplate is extra os template */
		rc = tmplset_get_velist_for_os(tmpl, &ve_list);
	}
	if (rc == 0 && !string_list_empty(&ve_list)) {
		vztt_logger(0, 0, "The next CT(s) based on %s EZ OS template:", \
				tmpl->os->name);
		for (v = ve_list.tqh_first; v != NULL; v = v->e.tqe_next)
			vztt_logger(VZTL_EINFO, 0, "%s", v->s);
		rc = VZT_TMPL_INSTALLED;
	}

	string_list_clean(&ve_list);

	return rc;
}

/* remove os template <tmpl> from HN */
int vztt_remove_os_template(char *arg, struct options *opts)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_remove_os_template(arg, opts_vztt);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_remove_os_template(char *arg, struct options_vztt *opts_vztt)
{
	int rc = 0;
	char *ostemplate;
	char cmd[PATH_MAX+100];
	char *rpm = NULL;
	void *lockdata;
	int shared = 0;
	char progress_stage[PATH_MAX];

	struct global_config gc;
	struct vztt_config tc;

	struct tmpl_set *tmpl;
	struct Transaction *to;

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);

	ostemplate = (*arg == '.') ? arg + 1 : arg;

	snprintf(progress_stage, sizeof(progress_stage),
		PROGRESS_REMOVE_OSTEMPLATE, ostemplate);
	progress(progress_stage, 0, opts_vztt->progress_fd);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		goto cleanup_0;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		goto cleanup_0;

	/* is template area on shared GFS partition? */
	if ((rc = is_shared_fs(gc.template_dir, &shared)))
		goto cleanup_0;

	if (shared && !(opts_vztt->flags & OPT_VZTT_FORCE_SHARED)) {
		vztt_logger(0, 0, "Template area %s resides on the "
			"shared partition.\nUse --force-shared option "
			"to force template removing.", gc.template_dir);
		rc = VZT_TMPL_SHARED;
		goto cleanup_0;
	}

	/* load all templates */
	if ((rc = tmplset_load(gc.template_dir, ostemplate, NULL, \
			TMPLSET_LOAD_OS_LIST|TMPLSET_LOAD_APP_LIST, \
			&tmpl, opts_vztt->flags & ~OPT_VZTT_USE_VZUP2DATE)))
		goto cleanup_0;

	if (!(opts_vztt->flags & OPT_VZTT_FORCE)) {
		if ((rc = check_ostemplate_toremove(tmpl)))
			goto cleanup_1;
		if (shared) {
			/* and check all available shared private
			   for template area on cluster case */
			if ((rc = check_shared_private_ostemplate(&gc, tmpl,
					opts_vztt)))
				goto cleanup_1;
		}
	}

	/* and remove template local caches */
	if ((rc = pm_init_wo_vzup2date(0, &gc, &tc, tmpl, opts_vztt, &to))) {
		if ((rc != VZT_ENVDIR_NFOUND) || !(opts_vztt->flags & OPT_VZTT_FORCE))
			goto cleanup_1;
	}

	/* lock OS template */
	if ((rc = tmpl_lock(&gc, tmpl->base,
			LOCK_WRITE, opts_vztt->flags, &lockdata)))
		goto cleanup_2;

	/* remove rpm, provides this template */
	if (tmpl_get_rpm((struct tmpl *)tmpl->os, &rpm) == 0) {
		snprintf(cmd, sizeof(cmd), RPMBIN " -e%s%s %s", \
			(opts_vztt->flags & OPT_VZTT_FORCE) ? " --nodeps" : "", \
			(opts_vztt->flags & OPT_VZTT_TEST) ? " --test" : "", rpm);
		execv_cmd(cmd, (opts_vztt->flags & OPT_VZTT_QUIET), 1);
	}
	if (!(opts_vztt->flags & OPT_VZTT_TEST)) {
		if (tmpl->os == (struct os_tmpl *)tmpl->base) {
			/* ok, we removed base template just now,
			and remove template area too */
			remove_directory(tmpl->base->basedir);
		} else {
			if (access(tmpl->os->confdir, F_OK) == 0)
				remove_directory(tmpl->os->confdir);
			to->pm_remove_local_caches(to, tmpl->os->reponame);
		}
		/* remove cache tarball too */

		tmpl_remove_cache_tar(gc.template_dir, tmpl->os->name);
	}

	tmpl_unlock(lockdata, opts_vztt->flags);
cleanup_2:
	VZTT_FREE_STR(rpm)
	pm_clean(to);
cleanup_1:
	tmplset_clean(tmpl);
cleanup_0:
	global_config_clean(&gc);
	vztt_config_clean(&tc);

	progress(progress_stage, 100, opts_vztt->progress_fd);

	return rc;
}

/* check TEMPLATES on available shared private area */
static int check_shared_private_templates(
	struct global_config *gc,
	struct tmpl_set *tmpl,
	const char *app,
	struct options_vztt *opts_vztt)
{
	char path[PATH_MAX+1];
	int rc = 0;
	char **plist;
	int i;
	struct ve_config vc;

	struct string_list used;
	struct string_list_el *s;

	ve_config_init(&vc);
	string_list_init(&used);

	vztt_logger(2, 0, "Template area resides on the shared partition. "
		"Will check all available private areas.");

	/* get private areas list */
	if ((plist = vzctl2_scan_private()) == NULL)
		return vztt_error(VZT_VZCTL_ERROR, 0,
			"vzctl2_scan_private() error: %s",
			vzctl2_get_last_error());

	for (i = 0; plist[i]; i++) {
		ve_config_clean(&vc);
		/* read config file */
		vztt_logger(2, 0, "Check private %s", plist[i]);
		snprintf(path, sizeof(path), "%s/" VE_CONFIG, plist[i]);
		if ((rc = ve_config_file_read(0, path, gc, &vc, 1))) {
			if (opts_vztt->flags & OPT_VZTT_FORCE)
				continue;
			else
				break;
		}
		/* find app template */
		if (string_list_find(&vc.templates, (char *)app) == NULL)
			continue;
		if ((rc = string_list_add(&used, plist[i])))
			break;
	}
	if (rc == 0 && !string_list_empty(&used)) {
		vztt_logger(0, 0, "The template %s installed into next "
			"CT private(s):", app);
		string_list_for_each(&used, s)
			vztt_logger(VZTL_EINFO, 0, "%s", s->s);
		rc = VZT_TMPL_INSTALLED;
	}

	for (i = 0; plist[i]; i++)
		free((void *)plist[i]);
	free((void *)plist);
	ve_config_clean(&vc);
	string_list_clean(&used);

	return rc;
}

/* remove app template <tmpl> from HN */
int vztt_remove_app_template(char *app, struct options *opts)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_remove_app_template(app, opts_vztt);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_remove_app_template(char *app, struct options_vztt *opts_vztt)
{
	int rc = 0;
	char cmd[PATH_MAX+100];
	char *rpm = NULL;
	void *lockdata;
	int shared = 0;
	char progress_stage[PATH_MAX];

	struct global_config gc;
	struct vztt_config tc;

	struct string_list templates;
	struct string_list ave_list;
	struct string_list ve_list;
	struct string_list_el *v;
	struct app_tmpl_list_el *a;

	struct tmpl_set *tmpl;
	struct Transaction *to;

	snprintf(progress_stage, sizeof(progress_stage),
		PROGRESS_REMOVE_APPTEMPLATE, app);
	progress(progress_stage, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	string_list_init(&ve_list);
	string_list_init(&ave_list);
	string_list_init(&templates);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		goto cleanup_0;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		goto cleanup_0;

	/* is template area on shared GFS partition? */
	if ((rc = is_shared_fs(gc.template_dir, &shared)))
		goto cleanup_0;

	if (shared && !(opts_vztt->flags & OPT_VZTT_FORCE_SHARED)) {
		vztt_logger(0, 0, "Template area %s resides on the "
			"shared partition.\nUse --force-shared option "
			"to force template removing.", gc.template_dir);
		rc = VZT_TMPL_SHARED;
		goto cleanup_0;
	}

	/* load all templates */
	if ((rc = tmplset_load(gc.template_dir, opts_vztt->for_obj, NULL, \
			TMPLSET_LOAD_OS_LIST|TMPLSET_LOAD_APP_LIST, \
			&tmpl, opts_vztt->flags & ~OPT_VZTT_USE_VZUP2DATE)))
		goto cleanup_0;

	/* check that this template really exist */
	if ((a = app_tmpl_list_find(&tmpl->avail_apps, app)) == NULL) {
		vztt_logger(0, 0, "EZ apptemplate %s for %s does not exist", \
			app, opts_vztt->for_obj);
		rc = VZT_TMPL_NOT_EXIST;
		goto cleanup_1;
	}

	/* and remove template local caches */
	if ((rc = pm_init_wo_vzup2date(0, &gc, &tc, tmpl, opts_vztt, &to))) {
		if ((rc != VZT_ENVDIR_NFOUND) || !(opts_vztt->flags & OPT_VZTT_FORCE))
			goto cleanup_1;
	}

	if (!(opts_vztt->flags & OPT_VZTT_FORCE)) {
		if ((rc = tmplset_get_velist_for_base(tmpl, &ve_list)))
			goto cleanup_2;

		/* Check for vz7 template - virtual app template */
		if (check_app_rpm_equal_os(tmpl, (struct tmpl *)a->tmpl)) {
			vztt_logger(0, 0, "The template %s is built into the "
				"OS template %s.\nYou can only remove it along with "
				"the entire OS template.", app, tmpl->os->name);
			rc = VZT_TMPL_NOT_EXIST;
			goto cleanup_2;
		}

		/* scan this ve's for target app template */
		for (v = ve_list.tqh_first; v != NULL; v = v->e.tqe_next) {
			if (ve_config_templates_read(v->s, &templates))
				goto cleanup_2;
			if (string_list_find(&templates, app))
				string_list_add(&ave_list, v->s);
			string_list_clean(&templates);
		}

		if (!string_list_empty(&ave_list)) {
			vztt_logger(0, 0, "The template %s installed into "
				"next CT(s):", app);
			for (v = ave_list.tqh_first; v != NULL;
						v = v->e.tqe_next)
				vztt_logger(VZTL_EINFO, 0, "%s", v->s);
			rc = VZT_TMPL_INSTALLED;
			goto cleanup_2;
		}

		if (shared) {
			if ((rc = check_shared_private_templates(&gc, tmpl,
					app, opts_vztt)))
				goto cleanup_2;
		}
	}

	/* lock base OS template */
	if ((rc = tmpl_lock(&gc, tmpl->base,
			LOCK_WRITE, opts_vztt->flags, &lockdata)))
		goto cleanup_2;

	/* remove rpm, provides this template */
	if (tmpl_get_rpm((struct tmpl *)a->tmpl, &rpm) == 0) {
		snprintf(cmd, sizeof(cmd), RPMBIN " -e%s%s %s", \
			(opts_vztt->flags & OPT_VZTT_FORCE) ? " --nodeps" : "", \
			(opts_vztt->flags & OPT_VZTT_TEST) ? " --test" : "", rpm);
		execv_cmd(cmd, (opts_vztt->flags & OPT_VZTT_QUIET), 1);
	}
	if (!(opts_vztt->flags & OPT_VZTT_TEST)) {
		if (access(a->tmpl->confdir, F_OK) == 0)
			remove_directory(a->tmpl->confdir);
		to->pm_remove_local_caches(to, a->tmpl->reponame);
	}

	tmpl_unlock(lockdata, opts_vztt->flags);
cleanup_2:
	VZTT_FREE_STR(rpm)
	pm_clean(to);
cleanup_1:
	tmplset_clean(tmpl);
cleanup_0:
	string_list_clean(&ave_list);
	string_list_clean(&ve_list);
	global_config_clean(&gc);
	vztt_config_clean(&tc);

	progress(progress_stage, 100, opts_vztt->progress_fd);

	return rc;
}

/* lock ostemplate on read */
int vztt_lock_ostemplate(const char *ostemplate, void **lockdata)
{
	int rc;
	struct global_config gc;
	struct tmpl_set *tmpl;

	/* struct initialization: should be first block */
	global_config_init(&gc);

	if (ostemplate == NULL)
		return VZT_BAD_PARAM;

	/* read global vz config */
	if ((rc = global_config_read(&gc, 0)))
		return rc;

	/* init only base and os templates */
	if ((rc = tmplset_init(gc.template_dir, (char *)ostemplate,
			NULL, 0, &tmpl, 0)))
		return rc;

       	/* lock all template area */
	rc = tmpl_lock(&gc, tmpl->base, LOCK_READ, 0, lockdata);

	tmplset_clean(tmpl);
	global_config_clean(&gc);

	return rc;
}

/* unlock ostemplate on read */
void vztt_unlock_ostemplate(void *lockdata)
{
	tmpl_unlock(lockdata, 0);
}

/* logfile & loglevel initialization */
void vztt_init_logger(const char * logfile, int loglevel)
{
	init_logger(logfile, loglevel);
}

/*
 Upgrade template area from vzfs3 to vzfs4
*/
int vztt_upgrade_area(
	char *ostemplate,
	struct options *opts)
{
	return VZT_UNSUPPORTED_VEFORMAT;
}

int vztt2_upgrade_area(
	char *ostemplate,
	struct options_vztt *opts_vztt)
{
	return VZT_UNSUPPORTED_VEFORMAT;
}

/* check vzfs version of directories in template area,
used for VE <ctid> */
int vztt_check_vzdir(const char *ctid, struct options *opts)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_check_vzdir(ctid, opts_vztt);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_check_vzdir(const char *ctid, struct options_vztt *opts_vztt)
{
	int rc = 0;
	char **vzdir;
	int i;
	struct stat st;

	/* get list of used by ve template area directories */
	if ((rc = vztt2_get_vzdir(ctid, opts_vztt, &vzdir)))
		return rc;

	for (i = 0; vzdir[i]; i++) {
		if (stat(vzdir[i], &st) == -1) {
			vztt_logger(0, errno, "stat(%s) failed", vzdir[i]);
			rc = VZT_CANT_LSTAT;
			goto cleanup;
		}
	}

cleanup:
	for (i = 0; vzdir[i]; i++)
		free((void *)vzdir[i]);
	free((void *)vzdir);
	return rc;
}

/* set default vztt options */
void vztt_set_default_options(struct options *opts)
{
	memset(opts, 0, sizeof(*opts));
	opts->logfile = VZPKGLOG;
	opts->debug = 1;
	opts->data_source = OPT_DATASOURCE_DEFAULT;
	opts->objects = OPT_OBJECTS_TEMPLATES;
	opts->templates = OPT_TMPL_OS | OPT_TMPL_APP;
	opts->clean = OPT_CLEAN_PKGS;
	opts->fld_mask = VZTT_INFO_NONE;
}

/* run <cmd> with <argv> from chroot() for <ostemplate> */
int vztt_run_from_chroot(
	const char *ostemplate,
	const char *cmd,
	const char *argv[],
	struct options *opts)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_run_from_chroot(ostemplate, cmd, argv, opts_vztt);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_run_from_chroot(
	const char *ostemplate,
	const char *cmd,
	const char *argv[],
	struct options_vztt *opts_vztt)
{
	int rc = 0;
	int i;
	char path[PATH_MAX+1];

	struct string_list args;
	struct string_list envs;

	struct global_config gc;
	struct vztt_config tc;

	struct tmpl_set *tmpl;

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	string_list_init(&args);
	string_list_init(&envs);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	/* load all templates */
	if ((rc = tmplset_load(gc.template_dir, (char *)ostemplate, NULL, 0,
			&tmpl, opts_vztt->flags)))
		return rc;

	snprintf(path, sizeof(path), VZ_PKGENV_DIR "%s",
			tmpl->base->package_manager);

	for (i = 0; argv[i]; i++)
		if ((rc = string_list_add(&args, (char *)argv[i])))
			goto cleanup_0;

	/* run cmd from chroot environment */
	rc = run_from_chroot2((char *)cmd, path, 1, 0, &args, &envs,
		tmpl->base->osrelease, NULL, NULL);

cleanup_0:
	tmplset_clean(tmpl);

	string_list_clean(&args);
	string_list_clean(&envs);

	return rc;
}


