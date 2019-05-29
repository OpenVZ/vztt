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
 * Template area cleanup
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
#include <sys/mman.h>
#include <asm/types.h>
#include <vzctl/libvzctl.h>

#include "util.h"
#include "tmplset.h"
#include "vztt.h"
#include "lock.h"
#include "queue.h"
#include "progress_messages.h"

/* 
 Clean local cache for all os and app templates
*/
static int clean_local_cache(char *ostemplate, struct options_vztt *opts_vztt)
{
	int rc = 0;
	int load_mask = 0, mark_mask = 0;
	void *lockdata;

	struct global_config gc;
	struct vztt_config tc;

	struct Transaction *to;
	struct tmpl_set *tmpl;

	progress(PROGRESS_CLEAN_CACHE, 0, opts_vztt->progress_fd);

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		goto cleanup_0;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		goto cleanup_0;

	/* select only OS, only app or both */
	mark_mask |= TMPLSET_MARK_OS | TMPLSET_MARK_OS_LIST;
	load_mask |= TMPLSET_LOAD_OS_LIST;
	mark_mask |= TMPLSET_MARK_AVAIL_APP_LIST;
	load_mask |= TMPLSET_LOAD_APP_LIST;

	/* load templates */
	if ((rc = tmplset_load(gc.template_dir, ostemplate, NULL, load_mask,\
			&tmpl, opts_vztt->flags & ~OPT_VZTT_USE_VZUP2DATE)))
		goto cleanup_0;

	if ((rc = tmplset_mark(tmpl, NULL, mark_mask, NULL)))
		goto cleanup_1;

	/* create & init package manager wrapper */
	if ((rc = pm_init(0, &gc, &tc, tmpl, opts_vztt, &to)))
		goto cleanup_1;

	/* prepare transaction for packages downloading */
	if ((rc = pm_create_tmp_root(to)))
		goto cleanup_2;

	/* lock */
	if ((rc = tmpl_lock(&gc, tmpl->base, 
			LOCK_WRITE, opts_vztt->flags, &lockdata)))
		goto cleanup_2;

	/* clean apt & yum local cache */
	if ((rc = to->pm_clean_local_cache(to)))
		goto cleanup_3;
cleanup_3:
	tmpl_unlock(lockdata, opts_vztt->flags);
cleanup_2:
	pm_clean(to);
cleanup_1:
	tmplset_clean(tmpl);
cleanup_0:
	vztt_config_clean(&tc);
	global_config_clean(&gc);

	progress(PROGRESS_CLEAN_CACHE, 100, opts_vztt->progress_fd);

	return rc;
}

/* template area cleanup: remove unused packages directories */
int vztt_cleanup(
	char *ostemplate,
	struct options *opts)
{
	int rc;
	struct options_vztt *opts_vztt;

	opts_vztt = options_convert(opts);
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	rc = vztt2_cleanup(ostemplate, opts_vztt);
	vztt_options_free(opts_vztt);
	return rc;
}

int vztt2_cleanup(
	char *ostemplate,
	struct options_vztt *opts_vztt)
{
	int rc;

	if (opts_vztt->clean & OPT_CLEAN_PKGS) {
		if ((rc = clean_local_cache(ostemplate, opts_vztt)))
			return rc;
	}
	return 0;
}
