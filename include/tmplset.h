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
 * Our contact details: Parallels International GmbH, Vordergasse 59, 8200
 * Schaffhausen, Switzerland.
 *
 * OS template set function declaration
 */

#include "vzcommon.h"
#include "template.h"
#include "transaction.h"
#include "options.h"
#include "config.h"

#ifndef _VZTT_TMPLSET_H_
#define _VZTT_TMPLSET_H_

#ifdef __cplusplus
extern "C" {
#endif
enum {
	TMPLSET_LOAD_OS_LIST = (1<<0),
	TMPLSET_LOAD_APP_LIST = (1<<1),
};

enum {
	TMPLSET_MARK_OS = (1<<0),
	TMPLSET_MARK_OS_LIST = (1<<1),
	TMPLSET_MARK_AVAIL_APP_LIST = (1<<2),
	TMPLSET_MARK_USED_APP_LIST = (1<<3),
};

enum {
	TMPLSET_DEF_MODE = 0,
	TMPLSET_SEPARATE_REPO_MODE = (1<<0),
};

struct tmpl_set {
	struct base_os_tmpl *base;
	struct os_tmpl *os;
	struct os_tmpl_list oses;
	struct app_tmpl_list avail_apps;
	struct app_tmpl_list used_apps;
	int mode;
};

/*
 read os and application templates names for os template human name <ostemplate>
 and init structures only
*/
int tmplset_init(
		char *tmpldir,
		char *ostemplate,
		struct string_list *apps,
		int mask,
		struct tmpl_set **tmpl,
		int flags);

/* clean template structure */
void tmplset_clean(struct tmpl_set *t);

/* install template(s) */
int tmplset_install(
		struct tmpl_set *tmpl,
		struct string_list *tmpls,
		char *ostemplate,
		int flags);

/* load template set data from files */
int tmplset_load(
		char *tmpldir,
		char *ostemplate,
		struct string_list *apps,
		int mask,
		struct tmpl_set **tmpl,
		int flags);
/*
 load template set data from files,
 according fields mask <fld_mask>
*/
int tmplset_selective_load(
		char *tmpldir,
		char *ostemplate,
		struct string_list *apps,
		int mask,
		struct tmpl_set **tmpl,
		struct options_vztt *opts_vztt);

/* mark templates in <t> according names from <ls>
   use os, oses, avail_apps and used_apps fields according <mask>
   not found names will add into <nf>, if it is not NULL
 */
int tmplset_mark(
		struct tmpl_set *t, 
		struct string_list *ls, 
		int mask, 
		struct string_list *nf);

/* unmark all templates */
int tmplset_unmark_all(struct tmpl_set *t);

/* get repositories & mirrorlists for marked templates */
int tmplset_get_urls(
		struct tmpl_set *t,
		struct repo_list *repositories,
		struct repo_list *zypp_repositories,
		struct repo_list *mirrorlist);

/* get names of marked templates with own repositories or mirrorlists, 
exclude base os template. used for ve or cache only */
int tmplset_get_repos(
		struct tmpl_set *t,
		struct string_list *ls);

/* get environments: if it does not defined in os, use base environment 
   This function does not copy envs struct, but return pointer only */
struct string_list *tmplset_get_envs(struct tmpl_set *t);

/* get template info */
int tmplset_get_info(
		struct global_config *gc,
		struct tmpl_set *t,
		struct url_map_list *url_map,
		struct tmpl_info *info);

/* update timestamp file in VE private area for marked templates */
int tmplset_update_privdir(
		struct tmpl_set *t,
		char *ve_private);

/* run scripts for marked templates (exclude oses)
   application scripts errors will ignore
   TODO: force option */
int tmplset_run_ve_scripts(
		struct tmpl_set *t,
		const char *ctid, 
		char *ve_root, 
		char *script,
		struct string_list *environment,
		int progress_fd);

/* Call OS template script */
int tmplset_run_ve0_scripts(
		struct tmpl_set *t,
		char *ve_root,
		const char *ctid,
		char *script,
		struct string_list *environment,
		int progress_fd);


/* detect indirectly installed app templates list via 
 full installed vz packages list
 1 - remove packages of os template and already installed app templates
     from full packages list
 2 - seek packages sets of available app templates in rest of packages list
     and remove if found. */
int tmplset_get_installed_apps(
		struct tmpl_set *t,
		struct Transaction *trans,
		char *ve_private,
		struct package_list *packages,
		struct string_list *apps);
/* add marked app templates from avail_app to used_apps list */
int tmplset_add_marked_apps(
		struct tmpl_set *t,
		char *ve_private,
		struct string_list *ls);
/* add marked app templates from avail_app to used_apps list
   and check indirectly installed templates */
int tmplset_add_marked_and_check_apps(
		struct tmpl_set *t,
		struct Transaction *trans,
		char *ve_private, 
		struct package_list *packages,
		struct string_list *existed);

/* remove app templates from used_apps list
   do not check nothing - only find and move */
int tmplset_remove_marked_apps(
		struct tmpl_set *t,
		char *ve_private,
		struct string_list *apps);
/* remove app templates from used_apps list
   and check indirectly removed templates */
int tmplset_remove_marked_and_check_apps(
		struct tmpl_set *t,
		struct Transaction *trans,
		char *ve_private,
		struct package_list *packages,
		struct string_list *existed);

/* check installed templates after packages install/remove */
int tmplset_check_apps(
		struct tmpl_set *t,
		struct Transaction *trans,
		char *ve_private,
		struct package_list *packages,
		struct string_list *existed);

/* find and load update template set <dst> for template set <src> */
int tmplset_find_upgrade(
		struct tmpl_set *src,
		int force,
		int packages,
		struct tmpl_set **dst);

/* check template from list <tl> are available */
int tmplset_check(
		struct tmpl_set *t,
		struct string_list *tl,
		int flags);
/* check that marked os and app templates are installed */
int tmplset_check_for_update(
		struct tmpl_set *t,
		struct string_list *tmpl,
		int flags);
/* check that marked app templates are installed */
int tmplset_check_for_remove(
		struct tmpl_set *t,
		struct string_list *apps,
		int flags);
/* check that marked app templates does not installed yet */
int tmplset_check_for_install(
		struct tmpl_set *t,
		struct string_list *apps,
		int flags);
/* get list of base and extra OS templates */
int tmplset_get_os_names(struct tmpl_set *t, struct string_list *names);

/* seek all base OS templates in <tmpldir> directory */
int tmplset_get_all_base(char *tmpldir, struct string_list *ls);

/* alloc template array */
int tmplset_alloc_list(size_t sz, struct tmpl_list_el ***ls);

/* get template list */
int tmplset_get_list(
		struct tmpl_set *t,
		struct global_config *gc,
		struct options_vztt *opts_vztt,
		struct tmpl_list_el ***ls);
/* get VE template list */
int tmplset_get_ve_list(
		struct tmpl_set *t,
		struct global_config *gc,
		char *ve_private,
		struct options_vztt *opts_vztt,
		struct tmpl_list_el ***ls);

/* get list of ve's, for which OSTEMPLATE is <t->os> */
int tmplset_get_velist_for_os(
		struct tmpl_set *t,
		struct string_list *ls);
/* get list of ve's, for which <OSTEMPLATE> is base or extra os.
Otherwise: <OSTEMPLATE> is <t->base> or <OSTEMPLATE> is in <t->oses> list */
int tmplset_get_velist_for_base(
		struct tmpl_set *t,
		struct string_list *ls);

/* get common packages list for marked templates */
int tmplset_get_marked_pkgs(struct tmpl_set *t, struct string_list *packages);

/* get common packages list for non marked templates */
int tmplset_get_ve_nonmarked_pkgs(
		struct tmpl_set *t,
		struct string_list *packages);

/* check template architecture */
int tmplset_check_arch(const char *arch);

/* lock tmplset */
int tmplset_lock(struct tmpl_set *t, int skiplock, int action);

/* unlock tmplset */
void tmplset_unlock(struct tmpl_set *t, int skiplock);

int check_ovz_cache(char *tmpldir, char *ostemplate, int just_check);

#ifdef __cplusplus
}
#endif

#endif

