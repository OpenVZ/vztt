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
 * config files data and read/write functions declarations
 */


#ifndef __VZCONFIG_H__
#define __VZCONFIG_H__

#include "vzcommon.h"
#include "options.h"
#include "queue.h"

#ifdef __cplusplus
extern "C" {
#endif

struct global_config
{
	char *lockdir;
	char *template_dir;
	char *ve_root;
	char *ve_private;
	char *http_proxy;
	char *http_proxy_user;
	char *http_proxy_password;
	unsigned long veformat;
	unsigned long velayout;
	int used_vefstype;
	unsigned int golden_image;
	struct string_list csum_white_list;
};

struct vztt_config
{
	struct url_map_list url_map;
	char *http_proxy;
	char *http_proxy_user;
	char *http_proxy_password;
	char *ftp_proxy;
	char *ftp_proxy_user;
	char *ftp_proxy_password;
	char *vztt_proxy;
	char *exclude;
	unsigned int skipi386;
	int metadata_expire;
	char *repair_mirror;
	int apptmpl_autodetect;
	unsigned long archive;
};

struct ve_config
{
	char *config;
	ctid_t ctid;
	char *ve_root;
	char *ve_private;
	char *ostemplate;
	char *distribution;
	struct string_list templates;
	unsigned long technologies;
	char *exclude;
	unsigned veformat;
	unsigned layout;
	int tmpl_type;
	unsigned long long diskspace; //in Kb
};


/* global OpenVZ config parameters */
void global_config_init(struct global_config *gc);
int global_config_read(
		struct global_config *gc,
		struct options_vztt *opts_vztt);
void global_config_clean(struct global_config *gc);
unsigned long get_cache_type(struct global_config *gc);

/* /etc/vztt/vztt.conf & /etc/vztt/url.map */
void vztt_config_init(struct vztt_config *tc);
int vztt_config_read(char *tmpldir, struct vztt_config *tc);
void vztt_config_clean(struct vztt_config *tc);


/* VE config data */
void ve_config_init(struct ve_config *vc);
void ve_config_clean(struct ve_config *vc);
/* load ve config data from file */
int ve_config_file_read(
		const char *ctid,
		char *path,
		struct global_config *gc,
		struct ve_config *vc,
		int ignore);
/* load VE config data */
int ve_config_read(
		const char *ctid, 
		struct global_config *gc, 
		struct ve_config *vc, 
		int ignore);
/* read OSTEMPLATE variable from ve config */
int ve_config_ostemplate_read(const char *ctid, char **ostemplate, int *tmpl_type);
/* read OSTEMLATE variable from ve sample */
int ve_file_config_ostemplate_read(char *sample, char **ostemplate);

/* read TEMPLATES variable from ve config */
int ve_config_templates_read(const char *ctid, struct string_list *templates);
/* read TEMPLATES variable from ve sample */
int ve_file_config_templates_read(char *sample, struct string_list *templates);
/* read GOLDEN_IMAGE variable from ve sample */
int ve_file_config_golden_image_read(char *sample, struct global_config *gc);

/* save OSTEMPLATES, DISTRIBUTION, TEMPLATES, TECHNOLOGIES and VEFORMAT
   in VE config */
int ve_config6_save(
		char const *path,
		char const *ostemplate,
		char const *distribution,
		struct string_list *apps,
		unsigned long technologies,
		unsigned long veformat);
/* save TEMPLATES in VE config */
int ve_config2_save(
		char const *path,
		struct string_list *apps);
/* save TEMPLATES in VE config */
int ve_config1_save(
		char const *path,
		struct string_list *apps);
/* read proxy configuration from:
1 - in global VZ config
2 - in VZTT config
3 - http_proxy, ftp_proxy and https_proxy environment variables */
int get_proxy(	struct global_config *gc,
		struct vztt_config *tc,
		struct _url *http_proxy,
		struct _url *ftp_proxy,
		struct _url *https_proxy);
/* mix technologies:
VZ_T_I386, VZ_T_X86_64, VZ_T_IA64, VZ_T_NPTL, VZ_T_SYSFS
- from OS template
VZ_T_SLM
- from VE config
VZ_T_ZDTM
- zero downtime migration - kernel feature, do not save at VE config
*/
extern unsigned long get_ve_technologies(
	unsigned long tmpl_technologies,
	unsigned long ve_technologies);

/* get list of ve's, use <selector> to select ve */
int get_ve_list(
		struct unsigned_list *ls,
		int selector(const char *ctid, void *data),
		void *data);

/* copy Container config file src to file dst and parse it */
int copy_container_config_file(const char *dst, const char *src);

#ifdef __cplusplus
};
#endif

#endif

