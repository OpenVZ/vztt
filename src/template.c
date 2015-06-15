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
 * OS and application template operation functions
 */

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <sys/utsname.h>
#include <dirent.h>
#include <error.h>
#include <limits.h>
#include <time.h>

#include "vzcommon.h"
#include "config.h"
#include "vztt_error.h"
#include "util.h"
#include "template.h"
#include "tmplset.h"
#include "vztt.h"
#include "lock.h"

/* to initialize common templates data */
static void init_tmpl(struct tmpl *tmpl)
{
	tmpl->marker = 0;
	tmpl->summary = NULL;
	string_list_init(&tmpl->description);
	string_list_init(&tmpl->packages);
	repo_list_init(&tmpl->repositories);
	repo_list_init(&tmpl->zypp_repositories);
	repo_list_init(&tmpl->mirrorlist);
	tmpl->golden_image = 1;
}

/* to initialize application template data */
int init_app_tmpl(
	struct app_tmpl *tmpl,
	char *name,
	char *confdir,
	struct app_tmpl *base)
{
	tmpl->confdir = strdup(confdir);
	tmpl->name = strdup(name);
	tmpl->base = (struct tmpl *)base;
	tmpl->reponame =  strdup(name);
	if ((tmpl->confdir == NULL) || (tmpl->name == NULL) || \
			(tmpl->reponame == NULL)) {
		vztt_logger(0, errno, "Cannot alloc memory");

                if(tmpl->confdir)
                    free(tmpl->confdir);
                if(tmpl->name)
                    free(tmpl->name);
                if(tmpl->reponame)
                    free(tmpl->reponame);

		return VZT_CANT_ALLOC_MEM;
	}
	init_tmpl((struct tmpl *)tmpl);

	return 0;
}

/* to initialize os template data */
int init_os_tmpl(
	struct os_tmpl *tmpl,
	char *confdir,
	char *setname,
	struct base_os_tmpl *base)
{
	size_t len;

	tmpl->confdir = strdup(confdir);
	len = strlen(base->name) + strlen(setname) + 2;
	tmpl->name = (char *)malloc(len);
	tmpl->setname = strdup(setname);
	tmpl->base = (struct tmpl *)base;
	tmpl->reponame =  strdup(setname);
	if ((tmpl->confdir == NULL) || (tmpl->name == NULL) || \
			(tmpl->setname == NULL) || (tmpl->reponame == NULL)) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	snprintf(tmpl->name, len, "%s-%s", base->name, setname);
	string_list_init(&tmpl->environment);
	string_list_init(&tmpl->packages0);
	string_list_init(&tmpl->packages1);
	init_tmpl((struct tmpl *)tmpl);

	return 0;
}

/* to initialize base os template data */
int init_base_os_tmpl(
	struct base_os_tmpl *tmpl,
	char *tmpldir,
	char *osname,
	char *osver,
	char *osarch)
{
	char buf[PATH_MAX+1];

	tmpl->tmpldir = strdup(tmpldir);
	tmpl->osname = strdup(osname);
	tmpl->osver = strdup(osver);
	tmpl->osarch = strdup(osarch);
	tmpl->reponame =  strdup(BASEREPONAME);
	if ((tmpl->tmpldir == NULL) || (tmpl->osname == NULL) || \
			(tmpl->osver == NULL) || (tmpl->osarch == NULL) || \
			(tmpl->reponame == NULL)) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	snprintf(buf, sizeof(buf), "%s/%s/%s", osname, osver, osarch);
	tmpl->basesubdir = strdup(buf);
	snprintf(buf, sizeof(buf), "%s/%s/%s/%s", \
			tmpldir, osname, osver, osarch);
	tmpl->basedir = strdup(buf);
	snprintf(buf, sizeof(buf), "%s/%s/%s/%s/config/os/%s", \
			tmpldir, osname, osver, osarch, DEFSETNAME);
	tmpl->confdir = strdup(buf);
	snprintf(buf, sizeof(buf), "%s-%s-%s", osname, osver, osarch);
	tmpl->name = strdup(buf);
	if ((tmpl->basesubdir == NULL) || (tmpl->basedir == NULL) || \
			(tmpl->confdir == NULL) || (tmpl->name == NULL)) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	tmpl->base = NULL;
	string_list_init(&tmpl->environment);
	string_list_init(&tmpl->packages0);
	string_list_init(&tmpl->packages1);
	tmpl->package_manager = NULL;
	tmpl->distribution = NULL;
	tmpl->technologies = 0;
	string_list_init(&tmpl->upgradable_versions);
	tmpl->osrelease = NULL;
	tmpl->jquota = NULL;
	tmpl->cache_type = VZT_CACHE_TYPE_ALL;
	init_tmpl((struct tmpl *)tmpl);

	return 0;
}

/* clean common templates data */
static void tmpl_clean(struct tmpl *tmpl)
{
	if (tmpl->summary)
		free((void *)tmpl->summary);
	string_list_clean(&tmpl->description);
	string_list_clean(&tmpl->packages);
	repo_list_clean(&tmpl->repositories);
	repo_list_clean(&tmpl->zypp_repositories);
	repo_list_clean(&tmpl->mirrorlist);
}

/* clean application template data */
void app_tmpl_clean(struct app_tmpl *tmpl)
{
	if (tmpl->confdir)
		free((void *)tmpl->confdir);
	if (tmpl->name)
		free((void *)tmpl->name);
	if (tmpl->reponame)
		free((void *)tmpl->reponame);
	tmpl_clean((struct tmpl *)tmpl);
}

/* clean os template data */
void os_tmpl_clean(struct os_tmpl *tmpl)
{
	if (tmpl->confdir)
		free((void *)tmpl->confdir);
	if (tmpl->name)
		free((void *)tmpl->name);
	if (tmpl->setname)
		free((void *)tmpl->setname);
	if (tmpl->reponame)
		free((void *)tmpl->reponame);
	string_list_clean(&tmpl->environment);
	string_list_clean(&tmpl->packages0);
	string_list_clean(&tmpl->packages1);
	tmpl_clean((struct tmpl *)tmpl);
}

/* clean base os template data */
void base_os_tmpl_clean(struct base_os_tmpl *tmpl)
{
	if (tmpl->tmpldir)
		free((void *)tmpl->tmpldir);
	if (tmpl->osname)
		free((void *)tmpl->osname);
	if (tmpl->osver)
		free((void *)tmpl->osver);
	if (tmpl->osarch)
		free((void *)tmpl->osarch);
	if (tmpl->reponame)
		free((void *)tmpl->reponame);
	if (tmpl->basesubdir)
		free((void *)tmpl->basesubdir);
	if (tmpl->basedir)
		free((void *)tmpl->basedir);
	if (tmpl->confdir)
		free((void *)tmpl->confdir);
	if (tmpl->name)
		free((void *)tmpl->name);

	string_list_clean(&tmpl->environment);
	string_list_clean(&tmpl->packages0);
	string_list_clean(&tmpl->packages1);
	if (tmpl->package_manager)
		free((void *)tmpl->package_manager);
	if (tmpl->distribution)
		free((void *)tmpl->distribution);
	string_list_clean(&tmpl->upgradable_versions);
	if (tmpl->osrelease)
		free((void *)tmpl->osrelease);
	if (tmpl->jquota)
		free((void *)tmpl->jquota);
	tmpl_clean((struct tmpl *)tmpl);
}

/* load common templates data from files */
static int load_tmpl(unsigned long fld_mask, struct tmpl *tmpl)
{
	char path[PATH_MAX+1];
	char *val = 0;
	int rc;
	int num = 0;

	/* read os template package list */
	if (fld_mask & VZTT_INFO_PACKAGES) {
		snprintf(path, sizeof(path), "%s/packages", tmpl->confdir);
		if (access(path, F_OK)) {
			vztt_logger(0, 0, "Can not find packages file for "\
				"%s EZ template", tmpl->name);
			return VZT_TMPL_BROKEN;
		}
		if ((rc = string_list_read2(path, &tmpl->packages)) != 0) {
			vztt_logger(0, 0, "Can not read packages file for "\
				"%s EZ template", tmpl->name);
			return VZT_TMPL_BROKEN;
		}
	}

	/* read repositories and mirrorlist */
	if (fld_mask & VZTT_INFO_REPOSITORIES) {
		snprintf(path, sizeof(path), "%s/repositories", tmpl->confdir);
		repo_list_read(path, tmpl->reponame, &num, &tmpl->repositories);
		snprintf(path, sizeof(path), "%s/zypp_repositories", tmpl->confdir);
		repo_list_read(path, tmpl->reponame, &num, &tmpl->zypp_repositories);
	}
	if (fld_mask & VZTT_INFO_MIRRORLIST) {
		snprintf(path, sizeof(path), "%s/mirrorlist", tmpl->confdir);
		repo_list_read(path, tmpl->reponame, &num, &tmpl->mirrorlist);
	}

	/* description & summary */
	if (fld_mask & VZTT_INFO_DESCRIPTION) {
		snprintf(path, sizeof(path), "%s/description", tmpl->confdir);
		string_list_read(path, &tmpl->description);
	}
	if (fld_mask & VZTT_INFO_SUMMARY) {
		snprintf(path, sizeof(path), "%s/summary", tmpl->confdir);
		read_string(path, &tmpl->summary);
	}

	/* Check for golden image support */
	if (fld_mask & VZTT_INFO_GOLDEN_IMAGE) {
		snprintf(path, sizeof(path), "%s/golden_image", tmpl->confdir);
		read_string(path, &val);
		if (is_disabled(val))
			tmpl->golden_image = 0;
		free(val);
	}

	return 0;
}

/* load application template data from files */
int load_app_tmpl(unsigned long fld_mask, struct app_tmpl *tmpl)
{
	return load_tmpl(fld_mask, (struct tmpl *)tmpl);
}

/* load os template data from files */
int load_os_tmpl(unsigned long fld_mask, struct os_tmpl *tmpl)
{
	char path[PATH_MAX+1];
	int rc;

	/* read common part */
	if ((rc = load_tmpl(fld_mask, (struct tmpl *)tmpl)))
		return rc;

	if (fld_mask & VZTT_INFO_ENVIRONMENT) {
		snprintf(path, sizeof(path), "%s/environment", tmpl->confdir);
		string_list_read(path, &tmpl->environment);
	}
	if (fld_mask & VZTT_INFO_PACKAGES0) {
		snprintf(path, sizeof(path), "%s/packages_0", tmpl->confdir);
		string_list_read(path, &tmpl->packages0);
	}
	if (fld_mask & VZTT_INFO_PACKAGES1) {
		snprintf(path, sizeof(path), "%s/packages_1", tmpl->confdir);
		string_list_read2(path, &tmpl->packages1);
	}

	return 0;
}

/* Return journalled quota unsupport for known OS templates */
static int known_jquota(char *osname, char *osver, int *jquota)
{
	FILE *fp;
	char str[STRSIZ];
	char tmpl_str[STRSIZ];

	if ((fp = fopen(JQUOTA_CONFIGURATION_PATH, "r")) == NULL)
	{
		vztt_logger(0, errno, "Cannot find jquota configuration file %s : %s", \
			JQUOTA_CONFIGURATION_PATH);
		return VZT_FILE_NFOUND;
	}

	snprintf(tmpl_str, sizeof(tmpl_str), "%s-%s", osname, osver);

	// By default all templates should support journalled quota
	*jquota = 1;

	while (fgets(str, sizeof(str), fp))
	{
		if (str[0] == '#' || str[0] == '\n')
			continue;

		char *p = str;
		while (*p && isspace(*p))
			p++;

		char *sp = p;
		while (*sp && !isspace(*sp))
			sp++;
		*sp = '\0';

		if (strncmp(tmpl_str, p, sizeof(tmpl_str)) == 0)
		{
			*jquota = 0;
			break;
		}
	}

	fclose(fp);

	return 0;
}

/* load base os template data from files */
int load_base_os_tmpl(unsigned long fld_mask, struct base_os_tmpl *tmpl)
{
	char path[PATH_MAX+1];
	char *cache_type;
	int rc;
	struct string_list technologies;
	struct string_list_el *p;
	unsigned long tech;

	/* read common os template part */
	if ((rc = load_os_tmpl(fld_mask, (struct os_tmpl *)tmpl)))
		return rc;

	/* mandatory read package manager */
	snprintf(path, sizeof(path), "%s/package_manager", 
			tmpl->confdir);
	if (access(path, F_OK)) {
		vztt_logger(0, 0, "Can not find package_manager file "\
			"for %s EZ template", tmpl->name);
		return VZT_TMPL_BROKEN;
	}
	if ((rc = read_string(path, &tmpl->package_manager))) {
		vztt_logger(0, 0, "Can not read package_manager file "\
			"for %s EZ os template", tmpl->name);
		return VZT_TMPL_BROKEN;
	}

#if 0 // Temporary disabled due to incompatibility of rpms libdb
	/* Backward compatibility for 32-bit templates and pkgmans */
	if ((cache_type = strstr(tmpl->package_manager, "x86")))
	{
		cache_type[1] = '6';
		cache_type[2] = '4';
	}
	else if ((cache_type = strstr(tmpl->package_manager, "x64")) == 0)
	{
		cache_type = malloc(strlen(tmpl->package_manager) + 4);
		if (cache_type == NULL)
			return VZT_CANT_ALLOC_MEM;
		snprintf(cache_type, strlen(tmpl->package_manager) + 4,
			"%sx64", tmpl->package_manager);
		free((void *)tmpl->package_manager);
		tmpl->package_manager = cache_type;
	}
#endif

	if (fld_mask & VZTT_INFO_OSRELEASE) {
		snprintf(path, sizeof(path), "%s/osrelease", tmpl->confdir);
		read_string(path, &tmpl->osrelease);
	}

	if (fld_mask & VZTT_INFO_JQUOTA) {
		int jquota;
		if ((rc = known_jquota(tmpl->osname, tmpl->osver, &jquota)))
		{
			vztt_logger(0, 0, "Failed to detect journalled quota support " \
				"for EZ template %s", tmpl->name);
			return rc;
		}

		if (jquota)
			tmpl->jquota = strdup("yes");
		else
			tmpl->jquota = strdup("no");
	}

	if (fld_mask & VZTT_INFO_DISTRIBUTION) {
		snprintf(path, sizeof(path), "%s/distribution", tmpl->confdir);
		read_string(path, &tmpl->distribution);
	}

	if (fld_mask & VZTT_INFO_UPGRADABLE_VERSIONS) {
		snprintf(path, sizeof(path), "%s/upgradable_versions", 
			tmpl->confdir);
		if (!access(path, F_OK))
			string_list_read2(path, &tmpl->upgradable_versions);
	}

	if (fld_mask & VZTT_INFO_TECHNOLOGIES) {
		/* read technologies to temporary list */
		string_list_init(&technologies);
		snprintf(path, sizeof(path), "%s/technologies", tmpl->confdir);
		string_list_read2(path, &technologies);

		/* add technologies from package manager */
		snprintf(path, sizeof(path), VZ_PKGENV_DIR "%s/technologies", \
			tmpl->package_manager);
		string_list_read2(path, &technologies);
		/* convert list into ulong */
		for (p = technologies.tqh_first; p != NULL; p = p->e.tqe_next) {
			if ((tech = vzctl2_name2tech(p->s)) == 0) {
				vztt_logger(0, 0, "Unknown technology %s for "\
					"template %s", p->s, tmpl->name);
				return VZT_UNKNOWN_TECHNOLOGY;
			}
			tmpl->technologies += tech;
		}
		string_list_clean(&technologies);
	}

	/* Cache type. Ignore if unavailable */
	snprintf(path, sizeof(path), "%s/cache_type", tmpl->confdir);
	if (access(path, F_OK))
		return 0;

	if ((rc = read_string(path, &cache_type))) {
		vztt_logger(0, 0, "Can not read cache_type file " \
			"for %s EZ os template", tmpl->name);
		return VZT_TMPL_BROKEN;
	}

	tmpl->cache_type = atol(cache_type);
	free(cache_type);

	return 0;
}

/* get size of app template list */
size_t app_tmpl_list_size(struct app_tmpl_list *ls)
{
	struct app_tmpl_list_el *p;
	size_t sz = 0;

	for (p = ls->tqh_first; p != NULL; p = p->e.tqe_next)
		sz++;
	return sz;
}


/* get template info */
static int tmpl_get_info(
		struct tmpl *tmpl,
		struct url_map_list *url_map,
		struct tmpl_info *info)
{
	if (tmpl->name) {
		if ((info->name = strdup(tmpl->name)) == NULL) {
			vztt_logger(0, errno, "Cannot alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	}
	if (tmpl->summary) {
		if ((info->summary = strdup(tmpl->summary)) == NULL) {
			vztt_logger(0, errno, "Cannot alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	}

	if (url_map) {
		/* convert repositories & mirrorlists to human look
		   using replacement from url_map, if it's needs */
		struct string_list ls;
		char buffer[BUFSIZ];
		struct repo_rec *r;

		string_list_init(&ls);
		for (r = tmpl->repositories.tqh_first; r; r = r->e.tqe_next) {
			prepare_url(url_map, r->url, buffer, sizeof(buffer));
			string_list_add(&ls, buffer);
		}
		for (r = tmpl->zypp_repositories.tqh_first; r; r = r->e.tqe_next) {
			prepare_url(url_map, r->url, buffer, sizeof(buffer));
			string_list_add(&ls, buffer);
		}
		string_list_to_array(&ls, &info->repositories);
		string_list_clean(&ls);

		for (r = tmpl->mirrorlist.tqh_first; r; r = r->e.tqe_next) {
			prepare_url(url_map, r->url, buffer, sizeof(buffer));
			string_list_add(&ls, buffer);
		}
		string_list_to_array(&ls, &info->mirrorlist);
		string_list_clean(&ls);
	}
	string_list_to_array(&tmpl->description, &info->description);
	string_list_to_array(&tmpl->packages, &info->packages);
	if (tmpl->confdir) {
		if ((info->confdir = strdup(tmpl->confdir)) == NULL) {
			vztt_logger(0, errno, "Cannot alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	}

	return 0;
}

/* get application template info */
int app_tmpl_get_info(
		struct app_tmpl *app,
		struct url_map_list *url_map,
		struct tmpl_info *info)
{
	int rc;

	if ((rc = tmpl_get_info((struct tmpl *)app, url_map, info)))
		return rc;
	return 0;
}

/* get extra OS template info */
int os_tmpl_get_info(
		struct global_config *gc,
		struct os_tmpl *os,
		struct base_os_tmpl *base,
		struct url_map_list *url_map,
		struct tmpl_info *info)
{
	int rc;
	size_t i, j, sz;
	const char *t;
	char path[PATH_MAX+1];

	if ((rc = tmpl_get_info((struct tmpl *)os, url_map, info)))
		return rc;

	string_list_to_array(&os->environment, &info->environment);
	string_list_to_array(&os->packages0, &info->packages0);
	string_list_to_array(&os->packages1, &info->packages1);

	/* get technologies from base OS template */
	for (i = 0, sz = 0; available_technologies[i]; i++)
		if (base->technologies & available_technologies[i])
			sz++;

	if ((info->technologies = (char **)calloc(sz + 1, sizeof(char *)))\
			 == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	for (i = 0, j = 0; available_technologies[i] && j < sz; i++) {
		if (base->technologies & available_technologies[i]) {
			t = vzctl2_tech2name(available_technologies[i]);
			if ((info->technologies[j++] = strdup(t)) == NULL) {
				vztt_logger(0, errno, "Cannot alloc memory");
				return VZT_CANT_ALLOC_MEM;
			}
		}
	}
	info->technologies[j] = NULL;

	if (tmpl_get_cache_tar(gc, path, sizeof(path), base->tmpldir,
		os->name) == 0)
		info->cached = strdup("yes");
	else
		info->cached = strdup("no");

	if (base->osname)
		info->osname = strdup(base->osname);
	if (base->osver)
		info->osver = strdup(base->osver);
	if (base->osarch)
		info->osarch = strdup(base->osarch);
	if (base->package_manager) {
		info->package_manager = strdup(base->package_manager);
		if (PACKAGE_MANAGER_IS_RPM_ZYPP(base->package_manager))
			info->package_manager_type = strdup(RPM_ZYPP);
		else if (PACKAGE_MANAGER_IS_RPM(base->package_manager))
			info->package_manager_type = strdup(RPM);
		else if (PACKAGE_MANAGER_IS_DPKG(base->package_manager))
			info->package_manager_type = strdup(DPKG);
	}
	if (base->distribution)
		info->distribution = strdup(base->distribution);
	else if (base->osrelease)
		info->distribution = strdup(base->osrelease);
	else if (base->jquota)
		info->distribution = strdup(base->jquota);
	string_list_to_array(&base->upgradable_versions, \
		&info->upgradable_versions);

	return 0;
}

/* get os template timestamp from cache tarball */
static int os_tmpl_get_timestamp(
		struct global_config *gc,
		char *tmpldir,
		unsigned long fld_mask,
		struct tmpl_list_el *el)
{
	char buf[PATH_MAX+1];
	struct stat st;
	struct tm *lt;

	el->timestamp = NULL;
	if (fld_mask != VZTT_INFO_NONE)
		return 0;

	if (tmpl_get_cache_tar(gc, buf, sizeof(buf),
		tmpldir, el->info->name) != 0)
		// No any cache found
		return 0;

	if (stat(buf, &st))
		return 0;

	/* get timestamp from tarball mtime */
	lt = localtime(&st.st_mtime);

	if (lt == NULL)
		return 0;

	snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", \
		lt->tm_year+1900, lt->tm_mon+1, lt->tm_mday, \
		lt->tm_hour, lt->tm_min, lt->tm_sec);

	if ((el->timestamp = strdup(buf)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	return 0;
}

/* get timestamp for installed template */
static int ve_tmpl_get_timestamp(
		char *ve_private,
		unsigned long fld_mask,
		struct tmpl_list_el *el)
{
	char buf[PATH_MAX+1];
	char *p;
	FILE *fp;

	if (fld_mask != VZTT_INFO_NONE)
		return 0;

	snprintf(buf, sizeof(buf), "%s/templates/%s/timestamp", \
			ve_private, el->info->name);

	/* read timestamp files */
	if ((fp = fopen(buf, "r")) == NULL)
		return 0;
	if ((fgets(buf, sizeof(buf), fp)) == NULL)
		return 0;
	fclose(fp);
	if ((p = strchr(buf, '\n')))
		*p = '\0';
	if ((el->timestamp = strdup(buf)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	return 0;
}

/* fill template list element struct for application */
int app_tmpl_get_list_el(
		struct app_tmpl *app,
		char *path,
		unsigned long fld_mask,
		int installed,
		struct tmpl_list_el *el)
{
	int rc = 0;

	/* get selected info */
	if ((rc = app_tmpl_get_info(app, NULL, el->info)))
		return rc;

	if (installed)
		rc = ve_tmpl_get_timestamp(path, fld_mask, el);

	return rc;
}

/* fill template list element struct for os */
int os_tmpl_get_list_el(
		struct global_config *gc,
		struct os_tmpl *os,
		struct base_os_tmpl *base,
		char *tmpldir,
		unsigned long fld_mask,
		int installed,
		struct tmpl_list_el *el)
{
	int rc = 0;

	el->is_os = 1;

	/* get selected info */
	if ((rc = os_tmpl_get_info(gc, os, base, NULL, el->info)))
		return rc;

	if (installed)
	{
		rc = ve_tmpl_get_timestamp(tmpldir, fld_mask, el);
	}
	else
	{
		rc = os_tmpl_get_timestamp(gc, tmpldir, fld_mask, el);
	}

	return rc;
}

/* copy repo_rec * from <src> to <dst> */
int copy_url(struct repo_list *dst, struct repo_list *src)
{
	int rc;
	struct repo_rec *p;

	for (p = src->tqh_first; p != NULL; p = p->e.tqe_next) {
		if (repo_list_find(dst, p->url))
			continue;
		if ((rc = repo_list_add(dst, p->url, p->id, p->num)))
			return rc;
	}
	return 0;
}

/*
OS template list
*/
/* add <tmpl> in tail of <ls>
  This function does not clone <tmpl> */
int os_tmpl_list_add(struct os_tmpl_list *ls, struct os_tmpl *tmpl)
{
	struct os_tmpl_list_el *p;

	p = (struct os_tmpl_list_el *)malloc(sizeof(struct os_tmpl_list_el));
	if (p == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	p->tmpl = tmpl;
	TAILQ_INSERT_TAIL(ls, p, e);

	return 0;
}

/* find os template with name <name> in list <ls> */
struct os_tmpl_list_el *os_tmpl_list_find(
		struct os_tmpl_list *ls, 
		char *name)
{
	struct os_tmpl_list_el *p;

	for (p = ls->tqh_first; p != NULL; p = p->e.tqe_next) {
		if (strcmp(name, p->tmpl->name) == 0)
			return p;
	}
	return NULL;
}

/* remove element <el> from list <ls> and return pointer to previous elem
   This function does not free content */
struct os_tmpl_list_el *os_tmpl_list_remove(
		struct os_tmpl_list *ls,
		struct os_tmpl_list_el *el)
{
	/* get previous element */
	struct os_tmpl_list_el *prev = *el->e.tqe_prev;

	TAILQ_REMOVE(ls, el, e);
	free((void *)el);

	return prev;
}

/* get size of os template list */
size_t os_tmpl_list_size(struct os_tmpl_list *ls)
{
	struct os_tmpl_list_el *p;
	size_t sz = 0;

	for (p = ls->tqh_first; p != NULL; p = p->e.tqe_next)
		sz++;
	return sz;
}

/* clean list */
void os_tmpl_list_clean(struct os_tmpl_list *ls)
{
	struct os_tmpl_list_el *el;

	while (ls->tqh_first != NULL) {
		el = ls->tqh_first;
		TAILQ_REMOVE(ls, ls->tqh_first, e);
		free((void *)el);
	}
}

/*
APP templates list
*/
/* add <tmpl> in tail of <ls>
  This function does not clone <tmpl> */
int app_tmpl_list_add(struct app_tmpl_list *ls, struct app_tmpl *tmpl)
{
	struct app_tmpl_list_el *p;

	p = (struct app_tmpl_list_el *)malloc(sizeof(struct app_tmpl_list_el));
	if (p == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	p->tmpl = tmpl;
	TAILQ_INSERT_TAIL(ls, p, e);

	return 0;
}

/* find app template with name <name> in list <ls> */
struct app_tmpl_list_el *app_tmpl_list_find(
		struct app_tmpl_list *ls, 
		char *name)
{
	struct app_tmpl_list_el *p;

	for (p = ls->tqh_first; p != NULL; p = p->e.tqe_next) {
		if (strcmp(name, p->tmpl->name) == 0)
			return p;
	}
	return NULL;
}

/* remove element <el> from list <ls> and return pointer to previous elem
   This function does not free content */
struct app_tmpl_list_el *app_tmpl_list_remove(
		struct app_tmpl_list *ls,
		struct app_tmpl_list_el *el)
{
	/* get previous element */
	struct app_tmpl_list_el *prev = *el->e.tqe_prev;

	TAILQ_REMOVE(ls, el, e);
	free((void *)el);

	return prev;
}

/* clean list */
void app_tmpl_list_clean(struct app_tmpl_list *ls)
{
	struct app_tmpl_list_el *el;

	while (ls->tqh_first != NULL) {
		el = ls->tqh_first;
		TAILQ_REMOVE(ls, ls->tqh_first, e);
		free((void *)el);
	}
}


/* remove directory for template <name> in VE private area <ve_private> */
int remove_tmpl_privdir(char *ve_private, char *name)
{
	char buf[PATH_MAX+1];

	snprintf(buf, sizeof(buf), "%s/templates/%s", ve_private, name);
	remove_directory(buf);

	return 0;
}

/* update timestamp file for template <name> in VE private area <ve_private> */
int update_tmpl_privdir(char *ve_private, char *name)
{
	FILE *fp;
	char buf[PATH_MAX+1];
	struct stat st;
	time_t tm;
	struct tm *lt;

	snprintf(buf, sizeof(buf), "%s/templates/%s", ve_private, name);
	if (stat(buf, &st)) {
		if (mkdir(buf, 0755)) {
			vztt_logger(0, errno, "mkdir(%s) error", buf);
			return VZT_CANT_CREATE;
		}
	}
	else {
		if (!S_ISDIR(st.st_mode)) {
			vztt_logger(0, errno, "%s exist, but is not directory", buf);
			return VZT_NOT_DIR;
		}
	}

	/* create timestamp files */
	tm = time(NULL);
	lt = localtime(&tm);
	strncat(buf, "/timestamp", sizeof(buf)-strlen(buf)-1);
	if ((fp = fopen(buf, "w")) == NULL) {
		error(0, errno, "fopen(%s) error", buf);
		return VZT_CANT_OPEN;
	}
	fprintf(fp, "%04d-%02d-%02d %02d:%02d:%02d\n", \
		lt->tm_year+1900, lt->tm_mon+1, lt->tm_mday, \
		lt->tm_hour, lt->tm_min, lt->tm_sec);
	fclose(fp);
	chmod(buf, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

	return 0;
}

/* Call VE template script */
int run_ve_scripts(
		struct tmpl *t,
		const char *ctid, 
		char *ve_root, 
		char *script,
		struct string_list *environment,
		int progress_fd)
{
	char buf[PATH_MAX+1];

	snprintf(buf, sizeof(buf), "%s/%s", t->confdir, script);
	if (access(buf, X_OK) == 0)
		return call_VE_script(ctid, buf, ve_root, environment,
			progress_fd);

	if (t->base == NULL)
		return 0;

	/* for extra os & app templates try to run script for base template */
	snprintf(buf, sizeof(buf), "%s/%s", t->base->confdir, script);
	return call_VE_script(ctid, buf, ve_root, environment, progress_fd);
}

/* Call VE0 template script */
int run_ve0_scripts(
		struct os_tmpl *t,
		char *ve_root,
		const char *ctid,
		char *script,
		struct string_list *environment,
		int progress_fd)
{
	char buf[PATH_MAX+1];

	snprintf(buf, sizeof(buf), "%s/%s", t->confdir, script);
	if (access(buf, X_OK) == 0)
		return call_VE0_script(buf, ve_root, ctid, environment,
			progress_fd);

	if (t->base == NULL)
		return 0;

	/* for extra os & app templates try to run script for base template */
	snprintf(buf, sizeof(buf), "%s/%s", t->base->confdir, script);
	return call_VE0_script(buf, ve_root, ctid, environment, progress_fd);
}

/* is <path>/<name> a regular file? */
static int is_file(char *path, char *name)
{
	char buf[PATH_MAX+1];
	struct stat st;

	snprintf(buf, sizeof(buf), "%s/%s", path, name);

	if (stat(buf, &st))
		return 0;
	return S_ISREG(st.st_mode);
}

/* is <dir> template config directory? */
int is_tmpl(char *dir)
{
	return is_file(dir, "packages");
}

/* is <dir> base OS template config directory? */
int is_base_os_tmpl(char *dir)
{
	if (!is_tmpl(dir))
		return 0;
	return is_file(dir, "package_manager");
}

/* get name of rpm, provides this template */
int tmpl_get_rpm(struct tmpl *tmpl, char **rpm)
{
	char buf[PATH_MAX+100];
	int rc;
	FILE *fd;
	char *p;

	snprintf(buf, sizeof(buf), "rpm -qf %s/packages", tmpl->confdir);
	vztt_logger(4, 0, "%s", buf);
	if ((fd = popen(buf, "r")) == NULL) {
		vztt_logger(0, errno, "Error in popen(%s)", buf);
		return VZT_CANT_EXEC;
	}

	/* read first string only */
	buf[0] = '\0';
	p = fgets(buf, sizeof(buf), fd);
	rc = pclose(fd);
	if (WEXITSTATUS(rc))
		return VZT_CANT_EXEC;

	if ((p = strchr(buf, '\n')))
		*p = '\0';

	if ((*rpm = strdup(buf)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	return 0;
}

/* Compare version for package */
int compare_template_package_version(char *tname, char *package, char *version, int *eval)
{
	int rc = 0;
	void *lockdata;
	char *buf = 0;
	char *pv;

	struct package_list available;
	struct package_list empty;
	struct package_list_el *p;

	struct global_config gc;
	struct vztt_config tc;

	struct Transaction *to;
	struct tmpl_set *tmpl;

	struct options_vztt *opts_vztt;

	/* struct initialization: should be first block */
	global_config_init(&gc);
	vztt_config_init(&tc);
	package_list_init(&empty);
	package_list_init(&available);
	opts_vztt = vztt_options_create();

	/* Set quiet by default: do not print anything */
	opts_vztt->flags |= OPT_VZTT_QUIET;

	/* read global vz config */
	if ((rc = global_config_read(&gc, opts_vztt)))
		return rc;

	/* read vztt config */
	if ((rc = vztt_config_read(gc.template_dir, &tc)))
		return rc;

	/* load all OS templates */
	if ((rc = tmplset_load(gc.template_dir, tname, NULL, \
			TMPLSET_LOAD_OS_LIST, &tmpl, \
			opts_vztt->flags)))
		return rc;

	/* mark only OS template */
	if ((rc = tmplset_mark(tmpl, NULL, \
			TMPLSET_MARK_OS, NULL)))
		goto cleanup_0;

	/* create & init package manager wrapper */
	if ((rc = pm_init(0, &gc, &tc, tmpl, opts_vztt, &to)))
		goto cleanup_0;

	/* Especially for zypper: do not print ANYTHING! */
	to->debug = 0;
	to->ign_pm_err = 1;

	if ((rc = pm_create_tmp_root(to)))
		goto cleanup_1;

	/* We need only base os template metadata with any timestamp */
	if (check_metadata(tmpl->base->basedir, tmpl->base->name,
			METADATA_EXPIRE_MAX, opts_vztt->data_source))
	{
		if ((rc = tmpl_lock(&gc, tmpl->base,
				LOCK_WRITE, opts_vztt->flags, &lockdata)))
			goto cleanup_1;

		if ((rc = to->pm_update_metadata(to, tmpl->base->name)))
			goto cleanup_1;
	}
	else
	{
		if ((rc = tmpl_lock(&gc, tmpl->base,
				LOCK_READ, opts_vztt->flags, &lockdata)))
			goto cleanup_1;
	}

	rc = pm_modify(to, VZPKG_LIST, NULL, &available, &empty);
	tmpl_unlock(lockdata, opts_vztt->flags);
	if (rc)
		goto cleanup_1;

	for (p = available.tqh_first; p != NULL; p = p->e.tqe_next)
	{
		if (strcmp(p->p->name, package) == 0)
		{
			if ((buf = strdup(p->p->evr)) == NULL) {
				vztt_logger(0, errno, "Cannot alloc memory");
				rc = VZT_CANT_ALLOC_MEM;
				goto cleanup_1;
			}
			/* strip epoch */
			if ((pv = strchr(buf, ':')) == 0)
				pv = buf;
			else
				pv++;
			if ((rc = to->pm_ver_cmp(to, pv, version, eval)))
				goto cleanup_1;
			break;
		}
	}

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
	vztt_options_free(opts_vztt);
	VZTT_FREE_STR(buf);

	return rc;
}
