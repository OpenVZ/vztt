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
 * template module declarations
 */
#ifndef _VZTT_TEMPLATE_H_
#define _VZTT_TEMPLATE_H_

#include "vzcommon.h"
#include "queue.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STRUCT_TMPL_COMMON_FIELDS \
	int marker;\
	char *name;\
	char *confdir;\
	char *summary;\
	char *reponame;\
	struct string_list description;\
	struct string_list packages;\
	struct repo_list repositories;\
	struct repo_list zypp_repositories;\
	struct repo_list mirrorlist; \
	struct tmpl	*base; \
	unsigned int golden_image; \
	unsigned int no_pkgs_actions;

/* virtual template data structure */
struct tmpl {
	STRUCT_TMPL_COMMON_FIELDS
};

/* application template data structure */
struct app_tmpl {
	STRUCT_TMPL_COMMON_FIELDS
	char *setname;
};

#define STRUCT_OS_TMPL_COMMON_FIELDS \
	STRUCT_TMPL_COMMON_FIELDS \
	struct string_list environment; \
	/* packages{0,1} for debian OS template only */\
	struct string_list packages0;\
	struct string_list packages1;

/* os template data structure */
struct os_tmpl {
	STRUCT_OS_TMPL_COMMON_FIELDS
	char *setname;
	unsigned long cache_type;
};

/* base os template data structure */
struct base_os_tmpl {
	STRUCT_OS_TMPL_COMMON_FIELDS
	char *tmpldir;
	char *osname;
	char *osver;
	char *osarch;
	char *basesubdir;
	char *basedir;
	char *package_manager;
	char *distribution;
	unsigned long technologies;
	struct string_list upgradable_versions;
	char *osrelease;
	char *jquota;
	unsigned long cache_type;
	unsigned int multiarch;
};

/* to initialize application template data */
int init_app_tmpl(
	struct app_tmpl *tmpl,
	char *name,
	char *confdir,
	struct app_tmpl *base);
/* to initialize os template data */
int init_os_tmpl(
	struct os_tmpl *tmpl,
	char *confdir,
	char *setname,
	struct base_os_tmpl *base);
/* to initialize base os template data */
int init_base_os_tmpl(
	struct base_os_tmpl *tmpl,
	char *tmpldir,
	char *osname,
	char *osver,
	char *osarch);
/* clean application template data */
void app_tmpl_clean(struct app_tmpl *tmpl);
/* clean os template data */
void os_tmpl_clean(struct os_tmpl *tmpl);
/* clean base os template data */
void base_os_tmpl_clean(struct base_os_tmpl *tmpl);
/* load application template data from files */
int load_app_tmpl(unsigned long fld_mask, struct app_tmpl *tmpl);
/* load os template data from files */
int load_os_tmpl(unsigned long fld_mask, struct os_tmpl *tmpl);
/* load base os template data from files */
int load_base_os_tmpl(unsigned long fld_mask, struct base_os_tmpl *tmpl);
/* get application template info */
int app_tmpl_get_info(
		struct app_tmpl *app,
		struct url_map_list *url_map,
		struct tmpl_info *info);
/* get OS template info */
int os_tmpl_get_info(
		struct global_config *gc,
		struct os_tmpl *os,
		struct base_os_tmpl *base,
		struct url_map_list *url_map,
		struct tmpl_info *info);
/* fill template list element struct for application */
int app_tmpl_get_list_el(
		struct app_tmpl *app,
		char *path,
		unsigned long fld_mask,
		int installed,
		struct tmpl_list_el *el);
/* fill template list element struct for oses */
int os_tmpl_get_list_el(
		struct global_config *gc,
		struct os_tmpl *os,
		struct base_os_tmpl *base,
		char *path,
		unsigned long fld_mask,
		int installed,
		struct tmpl_list_el *el);
/* fill template list element struct for template installed into ve */
int tmpl_get_ve_list_el(
		struct tmpl *tmpl,
		char *ve_private,
		unsigned long fld_mask,
		struct tmpl_list_el *el);
/* get name of rpm, provides this template */
int tmpl_get_rpm(struct tmpl *tmpl, char **rpm);

/* copy repo_rec * from <src> to <dst> */
int copy_url(struct repo_list *dst, struct repo_list *src);


/* list of extra OS templates */
TAILQ_HEAD(os_tmpl_list, os_tmpl_list_el);
struct os_tmpl_list_el {
	struct os_tmpl *tmpl;
	TAILQ_ENTRY(os_tmpl_list_el) e;
};

/* list initialization */
static inline void os_tmpl_list_init(struct os_tmpl_list *ls)
{
	TAILQ_INIT(ls);
}

/* add <tmpl> in tail of <ls> */
int os_tmpl_list_add(struct os_tmpl_list *ls, struct os_tmpl *tmpl);

/* find os template with name <name> in list <ls> */
struct os_tmpl_list_el *os_tmpl_list_find(
		struct os_tmpl_list *ls, 
		char *name);

/* remove element <el> from list <ls> and return pointer to previous elem
   This function does not free content */
struct os_tmpl_list_el *os_tmpl_list_remove(
		struct os_tmpl_list *ls,
		struct os_tmpl_list_el *el);

/* get size of os template list */
size_t os_tmpl_list_size(struct os_tmpl_list *ls);

/* 1 if list is empty */
static inline int os_tmpl_list_empty(struct os_tmpl_list *ls)
{
	return (ls->tqh_first == NULL)?1:0;
}

/* clean list */
void os_tmpl_list_clean(struct os_tmpl_list *ls);

#define os_tmpl_list_for_each(ls, el) list_for_each(ls, el)


/* list of application templates */
TAILQ_HEAD(app_tmpl_list, app_tmpl_list_el);
struct app_tmpl_list_el {
	struct app_tmpl *tmpl;
	TAILQ_ENTRY(app_tmpl_list_el) e;
};

/* list initialization */
static inline void app_tmpl_list_init(struct app_tmpl_list *ls)
{
	TAILQ_INIT(ls);
}

/* add <tmpl> in tail of <ls> */
int app_tmpl_list_add(struct app_tmpl_list *ls, struct app_tmpl *tmpl);

/* find app template with name <name> in list <ls> */
struct app_tmpl_list_el *app_tmpl_list_find(
		struct app_tmpl_list *ls, 
		char *name);

/* remove element <el> from list <ls> and return pointer to previous elem
   This function does not free content */
struct app_tmpl_list_el *app_tmpl_list_remove(
		struct app_tmpl_list *ls,
		struct app_tmpl_list_el *el);

/* get size of app template list */
size_t app_tmpl_list_size(struct app_tmpl_list *ls);

/* 1 if list is empty */
static inline int app_tmpl_list_empty(struct app_tmpl_list *ls)
{
	return (ls->tqh_first == NULL)?1:0;
}

/* clean list */
void app_tmpl_list_clean(struct app_tmpl_list *ls);

#define app_tmpl_list_for_each(ls, el) list_for_each((ls), (el))


/* Call VE template script */
int run_ve_scripts(
		struct tmpl *t,
		const char *ctid, 
		char *ve_root, 
		char *script,
		struct string_list *environment,
		int progress_fd);
/* Call VE0 template script */
int run_ve0_scripts(
		struct os_tmpl *t,
		char *ve_root,
		const char *ctid,
		char *script,
		struct string_list *environment,
		int progress_fd);

/* remove directory for template <name> in VE private area <ve_private> */
int remove_tmpl_privdir(char *ve_private, char *name);
/* update timestamp file for template <name> in VE private area <ve_private> */
int update_tmpl_privdir(char *ve_private, char *name);

/* is <dir> template config directory? */
int is_tmpl(char *dir);
/* is <dir> base OS template config directory? */
int is_base_os_tmpl(char *dir);

int check_metadata(
		const char *basedir,
		const char *name,
		int metadata_expire,
		int data_source);

/* Compare version for package */
int compare_template_package_version(char *tname, char *package, char *version,
	int *eval);

#ifdef __cplusplus
}
#endif
#endif
