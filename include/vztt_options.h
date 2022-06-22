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
 * vztt options set function declaration
 */

#ifndef __VZTT_OPTIONS_H__
#define __VZTT_OPTIONS_H__

#ifdef __cplusplus
extern "C" {
#endif

struct options_vztt {
	int flags;
	char *logfile;
	int debug;
	int data_source;
	int objects;
	int templates;
	int clean;
	char *for_obj;
	unsigned long fld_mask;
	char *config;
	char *app_ostemplate;
	char *app_apptemplate;
	char *vefstype;
	char *image_format;
	int progress_fd;
};

#ifndef _USE_DLOPEN_

struct options_vztt *vztt_options_create(void);
void vztt_options_free(struct options_vztt *opts_vztt);

/* Set flags */
void vztt_options_set_quiet(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_force(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_force_shared(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_depends(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_test(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_skip_lock(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_custom_pkg(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_vz_dir(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_stdi_type(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_stdi_version(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_stdi_tech(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_stdi_format(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_only_std(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_with_std(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_cached_only(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_pkgid(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_force_openat(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_expanded(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_interactive(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_separate(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_update_cache(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_skip_db(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_use_vzup2date(int enabled, struct options_vztt *opts_vztt);
void vztt_options_set_available(int enabled, struct options_vztt *opts_vztt);

/* Set other */
void vztt_options_set_logfile(char *logfile, struct options_vztt *opts_vztt);
void vztt_options_set_debug(int debug, struct options_vztt *opts_vztt);
void vztt_options_set_data_source(int data_source, struct options_vztt *opts_vztt);
void vztt_options_set_objects(int objects, struct options_vztt *opts_vztt);
void vztt_options_set_templates(int templates, struct options_vztt *opts_vztt);
void vztt_options_set_clean(int clean, struct options_vztt *opts_vztt);
void vztt_options_set_for_obj(char *for_obj, struct options_vztt *opts_vztt);
void vztt_options_set_fld_mask(unsigned long fld_mask, struct options_vztt *opts_vztt);
void vztt_options_set_config(char *config, struct options_vztt *opts_vztt);
void vztt_options_set_app_ostemplate(char *app_ostemplate, struct options_vztt *opts_vztt);
void vztt_options_set_app_apptemplate(char *app_apptemplate, struct options_vztt *opts_vztt);
void vztt_options_set_vefstype(char *vefstype, struct options_vztt *opts_vztt);
void vztt_options_set_progress_fd(int progress_fd, struct options_vztt *opts_vztt);

#endif /* _USE_DLOPEN_ */

#ifdef __cplusplus
}
#endif

#endif /* _VZTT_OPTIONS_H_ */
