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
 * vztt internal options functions
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
#include "options.h"

/* vztt_options_set_* for external calls */
void vztt_options_set_quiet(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_QUIET;
	else
		opts_vztt->flags &=~ OPT_VZTT_QUIET;
}

void vztt_options_set_force(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_FORCE;
	else
		opts_vztt->flags &=~ OPT_VZTT_FORCE;
}

void vztt_options_set_force_shared(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_FORCE_SHARED;
	else
		opts_vztt->flags &=~ OPT_VZTT_FORCE_SHARED;
}

void vztt_options_set_depends(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_DEPENDS;
	else
		opts_vztt->flags &=~ OPT_VZTT_DEPENDS;
}

void vztt_options_set_test(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_TEST;
	else
		opts_vztt->flags &=~ OPT_VZTT_TEST;
}

void vztt_options_set_skip_lock(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_SKIP_LOCK;
	else
		opts_vztt->flags &=~ OPT_VZTT_SKIP_LOCK;
}

void vztt_options_set_custom_pkg(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_CUSTOM_PKG;
	else
		opts_vztt->flags &=~ OPT_VZTT_CUSTOM_PKG;
}

void vztt_options_set_vz_dir(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_VZ_DIR;
	else
		opts_vztt->flags &=~ OPT_VZTT_VZ_DIR;
}

void vztt_options_set_stdi_type(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_STDI_TYPE;
	else
		opts_vztt->flags &=~ OPT_VZTT_STDI_TYPE;
}

void vztt_options_set_stdi_version(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_STDI_VERSION;
	else
		opts_vztt->flags &=~ OPT_VZTT_STDI_VERSION;
}

void vztt_options_set_stdi_tech(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_STDI_TECH;
	else
		opts_vztt->flags &=~ OPT_VZTT_STDI_TECH;
}

void vztt_options_set_stdi_format(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_STDI_FORMAT;
	else
		opts_vztt->flags &=~ OPT_VZTT_STDI_FORMAT;
}

void vztt_options_set_only_std(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_ONLY_STD;
	else
		opts_vztt->flags &=~ OPT_VZTT_ONLY_STD;
}

void vztt_options_set_with_std(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_WITH_STD;
	else
		opts_vztt->flags &=~ OPT_VZTT_WITH_STD;
}

void vztt_options_set_cached_only(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_CACHED_ONLY;
	else
		opts_vztt->flags &=~ OPT_VZTT_CACHED_ONLY;
}

void vztt_options_set_pkgid(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_PKGID;
	else
		opts_vztt->flags &=~ OPT_VZTT_PKGID;
}

void vztt_options_set_force_openat(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_FORCE_OPENAT;
	else
		opts_vztt->flags &=~ OPT_VZTT_FORCE_OPENAT;
}

void vztt_options_set_expanded(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_EXPANDED;
	else
		opts_vztt->flags &=~ OPT_VZTT_EXPANDED;
}

void vztt_options_set_interactive(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_INTERACTIVE;
	else
		opts_vztt->flags &=~ OPT_VZTT_INTERACTIVE;
}

void vztt_options_set_separate(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_SEPARATE;
	else
		opts_vztt->flags &=~ OPT_VZTT_SEPARATE;
}

void vztt_options_set_update_cache(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_UPDATE_CACHE;
	else
		opts_vztt->flags &=~ OPT_VZTT_UPDATE_CACHE;
}

void vztt_options_set_skip_db(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_SKIP_DB;
	else
		opts_vztt->flags &=~ OPT_VZTT_SKIP_DB;
}

void vztt_options_set_use_vzup2date(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_USE_VZUP2DATE;
	else
		opts_vztt->flags &=~ OPT_VZTT_USE_VZUP2DATE;
}

void vztt_options_set_logfile(char *logfile, struct options_vztt *opts_vztt)
{
	char *new_logfile;

	if (logfile)
	{
		new_logfile = strdup(logfile);
		if (new_logfile)
		{
			if (opts_vztt->logfile)
				free(opts_vztt->logfile);
			opts_vztt->logfile = new_logfile;
		}
	}
}

void vztt_options_set_debug(int debug, struct options_vztt *opts_vztt)
{
	opts_vztt->debug = debug;
}

void vztt_options_set_data_source(int data_source, struct options_vztt *opts_vztt)
{
	opts_vztt->data_source = data_source;
}

void vztt_options_set_objects(int objects, struct options_vztt *opts_vztt)
{
	opts_vztt->objects = objects;
}

void vztt_options_set_templates(int templates, struct options_vztt *opts_vztt)
{
	opts_vztt->templates = templates;
}

void vztt_options_set_clean(int clean, struct options_vztt *opts_vztt)
{
	opts_vztt->clean = clean;
}

void vztt_options_set_for_obj(char *for_obj, struct options_vztt *opts_vztt)
{
	char *new_for_obj;

	if (for_obj)
	{
		new_for_obj = strdup(for_obj);
		if (new_for_obj)
		{
			if (opts_vztt->for_obj)
				free(opts_vztt->for_obj);
			opts_vztt->for_obj = new_for_obj;
		}
	}
}

void vztt_options_set_fld_mask(unsigned long fld_mask, struct options_vztt *opts_vztt)
{
	opts_vztt->fld_mask = fld_mask;
}

void vztt_options_set_config(char *config, struct options_vztt *opts_vztt)
{
	char *new_config;

	if (config)
	{
		new_config = strdup(config);
		if (new_config)
		{
			if (opts_vztt->config)
				free(opts_vztt->config);
			opts_vztt->config = new_config;
		}
	}
}

void vztt_options_set_app_ostemplate(char *app_ostemplate, struct options_vztt *opts_vztt)
{
	char *new_app_ostemplate;

	if (app_ostemplate)
	{
		new_app_ostemplate = strdup(app_ostemplate);
		if (new_app_ostemplate)
		{
			if (opts_vztt->app_ostemplate)
				free(opts_vztt->app_ostemplate);
			opts_vztt->app_ostemplate = new_app_ostemplate;
		}
	}
}

void vztt_options_set_app_apptemplate(char *app_apptemplate, struct options_vztt *opts_vztt)
{
	char *new_app_apptemplate;

	if (app_apptemplate)
	{
		new_app_apptemplate = strdup(app_apptemplate);
		if (new_app_apptemplate)
		{
			if (opts_vztt->app_apptemplate)
				free(opts_vztt->app_apptemplate);
			opts_vztt->app_apptemplate = new_app_apptemplate;
		}
	}
}

void vztt_options_set_vefstype(char *vefstype, struct options_vztt *opts_vztt)
{
	char *new_vefstype;

	if (vefstype)
	{
		new_vefstype = strdup(vefstype);
		if (new_vefstype)
		{
			if (opts_vztt->vefstype)
				free(opts_vztt->vefstype);
			opts_vztt->vefstype = new_vefstype;
		}
	}
}

void vztt_options_set_available(int enabled, struct options_vztt *opts_vztt)
{
	if (enabled)
		opts_vztt->flags |= OPT_VZTT_AVAILABLE;
	else
		opts_vztt->flags &=~ OPT_VZTT_AVAILABLE;
}

void vztt_options_set_progress_fd(int progress_fd, struct options_vztt *opts_vztt)
{
	opts_vztt->progress_fd = progress_fd;
}

/* Convert options to new format */
struct options_vztt *options_convert(struct options *opts)
{
	struct options_vztt *opts_vztt;

	opts_vztt = vztt_options_create();

	if (!opts_vztt)
		return 0;

	vztt_options_set_quiet(opts->quiet, opts_vztt);
	vztt_options_set_force(opts->force, opts_vztt);
	vztt_options_set_force_shared(opts->force_shared, opts_vztt);
	vztt_options_set_depends(opts->depends, opts_vztt);
	vztt_options_set_test(opts->test, opts_vztt);
	vztt_options_set_skip_lock(opts->skiplock, opts_vztt);
	vztt_options_set_custom_pkg(opts->custom_pkg, opts_vztt);
	vztt_options_set_vz_dir(opts->vz_dir, opts_vztt);
	vztt_options_set_stdi_type(opts->stdi_type, opts_vztt);
	vztt_options_set_stdi_version(opts->stdi_version, opts_vztt);
	vztt_options_set_stdi_tech(opts->stdi_tech, opts_vztt);
	vztt_options_set_stdi_format(opts->stdi_format, opts_vztt);
	vztt_options_set_only_std(opts->only_std, opts_vztt);
	vztt_options_set_with_std(opts->with_std, opts_vztt);
	vztt_options_set_cached_only(opts->cached_only, opts_vztt);
	vztt_options_set_pkgid(opts->pkgid, opts_vztt);
	vztt_options_set_force_openat(opts->force_openat, opts_vztt);
	vztt_options_set_expanded(opts->expanded, opts_vztt);
	vztt_options_set_interactive(opts->interactive, opts_vztt);
	vztt_options_set_separate(opts->separate, opts_vztt);
	vztt_options_set_update_cache(opts->update_cache, opts_vztt);
	vztt_options_set_skip_db(opts->skip_db, opts_vztt);
	vztt_options_set_logfile(opts->logfile, opts_vztt);
	opts_vztt->debug = opts->debug;
	opts_vztt->data_source = opts->data_source;
	opts_vztt->objects = opts->objects;
	opts_vztt->templates = opts->templates;
	opts_vztt->clean = opts->clean;
	vztt_options_set_for_obj(opts->for_obj, opts_vztt);
	opts_vztt->fld_mask = opts->fld_mask;

	return opts_vztt;
}

struct options_vztt *vztt_options_create()
{
	struct options_vztt *opts_vztt;
	opts_vztt = malloc(sizeof(*opts_vztt));
	if (!opts_vztt)
		return 0;
	memset(opts_vztt, 0, sizeof(*opts_vztt));
	opts_vztt->logfile = strdup(VZPKGLOG);
	if (!opts_vztt->logfile)
		return 0;
	opts_vztt->debug = 1;
	opts_vztt->data_source = OPT_DATASOURCE_DEFAULT;
	opts_vztt->objects = OPT_OBJECTS_TEMPLATES;
	opts_vztt->templates = OPT_TMPL_OS | OPT_TMPL_APP;
	opts_vztt->clean = OPT_CLEAN_PKGS;
	opts_vztt->fld_mask = VZTT_INFO_NONE;
	opts_vztt->flags |= OPT_VZTT_USE_VZUP2DATE;

	return opts_vztt;
}

void vztt_options_free(struct options_vztt *opts_vztt)
{
	if (opts_vztt->logfile)
		free(opts_vztt->logfile);

	if (opts_vztt->for_obj)
		free(opts_vztt->for_obj);

	if (opts_vztt->config)
		free(opts_vztt->config);

	if (opts_vztt->app_ostemplate)
		free(opts_vztt->app_ostemplate);

	if (opts_vztt->app_apptemplate)
		free(opts_vztt->app_apptemplate);

	if (opts_vztt->vefstype)
		free(opts_vztt->vefstype);

	if (opts_vztt)
		free(opts_vztt);
}
