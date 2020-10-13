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
 * metadata manipulation (update) module
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
#include <time.h>
#include <vzctl/libvzctl.h>

#include "vztt.h"
#include "util.h"
#include "tmplset.h"
#include "config.h"
#include "lock.h"

/* is metadata up2date? */
int check_metadata(
		const char *basedir,
		const char *name,
		int metadata_expire,
		int data_source)
{
	char path[PATH_MAX];
	struct stat st;

	if (data_source == OPT_DATASOURCE_REMOTE)
		return 1;

	snprintf(path, sizeof(path), "%s/" PM_DATA_SUBDIR PM_LIST_SUBDIR "%s", \
			basedir, name);
	if (stat(path, &st))
		/* although it may be other error, 
		suppose that file does not exist */
		return 1;

	if (data_source == OPT_DATASOURCE_LOCAL)
		return 0;
	if (time(NULL) - st.st_mtime > metadata_expire)
		/* expired */
		return 1;

	return 0;
}

/* find template with equals repo set in previously processed apptemplates
   and create symlink(s) on it metadata in success */
static int copy_existed_app_metadata(
		struct Transaction *to,
		struct tmpl_set *tmplset,
		struct tmpl *tmpl)
{
	int rc;
	char dst[PATH_MAX+1];
	struct app_tmpl_list_el *a;
	struct stat st;
	int retcode = 0;

	/* check all metadata type : if symlinks, remove it, 
	   to set <retcode>==1 otherwise */
	snprintf(dst, sizeof(dst), \
		"%s/" PM_DATA_SUBDIR PM_LIST_SUBDIR "%s", \
		tmplset->base->basedir, tmpl->name);
	if (lstat(dst, &st) == 0) {
		if (S_ISLNK(st.st_mode))
			unlink(dst);
		else
			retcode = 1;
	}
	retcode = to->pm_clean_metadata_symlinks(to, tmpl->name);
	if (retcode)
		return 1;

	for (a = tmplset->avail_apps.tqh_first; 
		a && ((struct tmpl *)a->tmpl != tmpl); a = a->e.tqe_next) 
	{
		if (repo_list_empty(&a->tmpl->repositories) && \
				repo_list_empty(&a->tmpl->mirrorlist))
			continue;

		/* compare repositories */
		if (repo_list_cmp(&a->tmpl->repositories, 
				&tmpl->repositories))
			continue;

		/* compare and mirrorlist too */
		if (repo_list_cmp(&a->tmpl->mirrorlist, 
				&tmpl->mirrorlist))
			continue;

		/* and copy metadata file */
		if ((symlink(a->tmpl->name, dst))) {
			return vztt_error(VZT_SYSTEM, errno, 
				"symlink(%s, %s)", a->tmpl->name, dst);
		}
		/* create symlinks for other metadata if needs (yum, #114487) */
		if ((rc = to->pm_clone_metadata(to, a->tmpl->name, tmpl->name)))
			return rc;
		return 0;
	}
	return -1;
}

/* update metadata for non-base os or app template */
static int update_secondary_metadata(
	struct tmpl *tmpl,
	struct global_config *gc,
	struct vztt_config *tc,
	struct tmpl_set *tmplset,
	int mask,
	void **lockdata,
	struct options_vztt *opts_vztt)
{
	int rc;
	struct Transaction *to;
	struct string_list ls;

	if (repo_list_empty(&tmpl->repositories) &&
			repo_list_empty(&tmpl->zypp_repositories) &&
			repo_list_empty(&tmpl->mirrorlist))
		return 0;

	if (check_metadata(tmplset->base->basedir, tmpl->name, 
			tc->metadata_expire, opts_vztt->data_source) == 0)
		return 0;

	string_list_init(&ls);

	if ((rc = string_list_add(&ls, tmpl->name)))
		return rc;
	if ((rc = tmplset_mark(tmplset, &ls, mask, NULL)))
		goto cleanup_0;
	if ((rc = pm_init(0, gc, tc, tmplset, opts_vztt, &to)))
		goto cleanup_1;
	if ((rc = pm_create_tmp_root(to)))
		goto cleanup_2;

	/* it's for plesk app templates: they include the same repos 
	in all templates. To avoid loading already loaded metadata,
	seek repo set of this app template in alredy updated */
	if (to->pm_clone_metadata && copy_existed_app_metadata(to, tmplset, tmpl) == 0)
		goto cleanup_2;

	if (*lockdata == NULL) {
		/* if can't lock - terminate all */
		if ((rc = tmpl_lock(gc, tmplset->base, 
				LOCK_WRITE, opts_vztt->flags, lockdata)))
			goto cleanup_1;
	}
	vztt_logger(2, 0, "update metadata for %s", tmpl->name);
	if (to->pm_update_metadata(to, tmpl->name))
		vztt_logger(0, 0, 
			"Can not update metadata for %s. Skipped.", tmpl->name);

cleanup_2:
	pm_clean(to);
cleanup_1:
	tmplset_unmark_all(tmplset);
cleanup_0:
	string_list_clean(&ls);
	return 0;
}

/* update OS and apps templates metadata
   ! lock template by request and unlock on exit
*/
int update_metadata(
	char *ostemplate,
	struct global_config *gc,
	struct vztt_config *tc,
	struct options_vztt *opts_vztt)
{
	int rc = 0;
	void *lockdata = NULL;

	struct tmpl_set *tmpl;
	struct Transaction *to;

	struct os_tmpl_list_el *o;
	struct app_tmpl_list_el *a;

	if (opts_vztt->data_source == OPT_DATASOURCE_LOCAL)
		return 0;

	/* load all templates */
	if ((rc = tmplset_load(gc->template_dir, ostemplate, NULL, \
			TMPLSET_LOAD_OS_LIST|TMPLSET_LOAD_APP_LIST, \
			&tmpl, opts_vztt->flags)))
		return rc;

	/* set special mode - do not use base os and os template 
	   repos by default */
	tmpl->mode |= TMPLSET_SEPARATE_REPO_MODE;

	/* step 1 : update metadata for base os templare only.
	   It's a critical step: exit on failure */
	if ((rc = tmplset_mark(tmpl, NULL, TMPLSET_MARK_OS, NULL)))
		goto cleanup_0;

	if ((rc = pm_init(0, gc, tc, tmpl, opts_vztt, &to)))
		goto cleanup_0;
	if ((rc = pm_create_tmp_root(to)))
		goto cleanup_1;

	/* load/update metadata from all repositories */
	if (check_metadata(tmpl->base->basedir, tmpl->base->name, 
			tc->metadata_expire, opts_vztt->data_source)) {
		if ((rc = tmpl_lock(gc, tmpl->base, 
				LOCK_WRITE, opts_vztt->flags, &lockdata)))
			goto cleanup_1;
		vztt_logger(2, 0, "update metadata for %s", tmpl->base->name);
		if ((rc = to->pm_update_metadata(to, tmpl->base->name)))
			goto cleanup_1;
	}

	tmplset_unmark_all(tmpl);
	pm_clean(to);

	/* step 2: update metadate for non-base os template
	   if it have repositories/mirrorlist. Errors ignored. */
	for (o = tmpl->oses.tqh_first; o != NULL; o = o->e.tqe_next) {
		if ((rc = update_secondary_metadata((struct tmpl *)o->tmpl, gc, tc, 
				tmpl, TMPLSET_MARK_OS_LIST, &lockdata, opts_vztt)))
			goto cleanup_0;
	}

	/* load available application template list */
	for (a = tmpl->avail_apps.tqh_first; a != NULL; a = a->e.tqe_next) {
		if ((rc = update_secondary_metadata((struct tmpl *)a->tmpl, gc, tc, 
			tmpl, TMPLSET_MARK_AVAIL_APP_LIST, &lockdata, opts_vztt)))
			goto cleanup_0;
	}
	goto cleanup_0;

cleanup_1:
	/* remove temporary dir */
	pm_clean(to);
cleanup_0:
	if (lockdata)
		tmpl_unlock(lockdata, opts_vztt->flags);
	tmplset_clean(tmpl);

	return rc;
}

/* update OS and apps templates metadata */
int vztt_update_metadata(
	char *ostemplate,
	struct options *opts)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_update_metadata(ostemplate, opts_vztt);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_update_metadata(
	char *ostemplate,
	struct options_vztt *opts_vztt)
{
	int rc = 0;

	struct global_config gc;
	struct vztt_config tc;

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	rc = update_metadata(ostemplate, &gc, &tc, opts_vztt);

	global_config_clean(&gc);
	vztt_config_clean(&tc);

	return rc;
}


