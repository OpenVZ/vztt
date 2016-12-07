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
 * OS template set operation functions
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
#include <string.h>

#include "vzcommon.h"
#include "config.h"
#include "vztt_error.h"
#include "util.h"
#include "tmplset.h"
#include "vztt.h"

/*
Templates are OS & Application templates.
OS templates are: Base & Extra OS templates.
Application templates are: Base & Extra Application templates.

Base OS template name is <osname>-<osver>-<osarch>
Extra OS template name is <osname>-<osver>-<osarch>-<extraname>
Extra OS template inherit from base: package_manager, distribution,
	technologies and upgradable_versions.
Base OS template environment will redefine by extra OS template environment.
Also repositories and mirrorlists of base OS template will added
to extras templates repositories and mirrorlists.

Base app template name is <appname>
Extra app template name is <appname>-<extraname>
Repositories and mirrorlists of base app template will added
to extras templates repositories and mirrorlists.

Template tree:

common part (basedir):
	/vz/template/<osname>/<osver>/<osarch>/
base os template subdirectory:		config/os/default
extra os template subdirectory:		config/os/<extraname>
base app template subdirectory:		config/app/<appname>/default
extra app template subdirectory: 	config/app/<appname>/<extraname>

Templates for one OS are intersection sets of packagea

*/

/* check template architecture */
int tmplset_check_arch(const char *arch)
{
	unsigned tech;

	/* check arch */
	if ((tech = vzctl2_name2tech(arch)) == 0) {
		vztt_logger(0, 0, "Unknown template architecture: %s", arch);
		return VZT_TMPL_UNKNOWN_ARCH;
	}
	if (vzctl2_check_tech(tech)) {
		vztt_logger(0, 0, "Unsupported template architecture: %s", arch);
		return VZT_TMPL_UNSUPPORTED_ARCH;
	}
	return 0;
}


/*
 For human os template name <ostemplate> find corresponding basedir and
 osname, osver, osarch, extraname
 Name variants are:
 <osname>-<osver>-<osarch>
 <osname>-<osver>-<osarch>-<extraname>
 <osname>-<osver>-<extraname>
 <osname>-<osver>

 Notes, osname and extraname can content '-' too
*/
static int parse_os_name(char *tmpldir,
		char *ostemplate,
		char **pname,
		char **pver,
		char **parch,
		char **pextraname)
{
	char *buf, *p, *t;
	char **tok = NULL;
	int ntok;
	int i, j, k;
	char *osname = NULL, *osver = NULL, *osarch = NULL, *extname = NULL;
	char defarch[100];
	char path[PATH_MAX+1];
	struct stat st;
	int lfound = 0;
	size_t sz;

	/* get token number */
	if ((buf = strdup(ostemplate)) == NULL)
		return vztt_error(VZT_CANT_ALLOC_MEM,
			errno, "Cannot alloc memory");
	for(p = buf, ntok = 0; p != NULL; p = strchr(p, '-')) {
		p++;
		ntok++;
	}
	free((void *)buf);
	if (ntok < 2) {
		vztt_logger(1, 0, "Can not find '%s' EZ os template", ostemplate);
		return VZT_TMPL_NOT_FOUND;
	}

	/* skip leading dot */
	p = (*ostemplate == '.')?ostemplate+1:ostemplate;
	sz = strlen(p)+1;
	buf = strdup(p);
	osname = strdup(p);
	extname = strdup(p);
	if ((buf == NULL) || (osname == NULL) || (extname == NULL)) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	if ((tok = (char **)calloc(ntok, sizeof(char *))) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	/* split name on tokens */
	for (p = buf, ntok = 0; p != NULL; ntok++) {
		t = strchr(p, '-');
		tok[ntok] = p;
		if (t)
			*(t++) = '\0';
		p = t;
	}

	strcpy(osname, tok[0]);
	/* get default arch (HN arch) */
	get_hw_arch(defarch, sizeof(defarch));
	for(i=0; i+1 < ntok; \
			strncat(osname, "-", sz-strlen(osname)-1), \
			strncat(osname, tok[i+1], sz-strlen(osname)-1), \
			i++) {
		j = i+1;
		osver = tok[j];
		/* does directory exist for osname & osver ? */
		snprintf(path, sizeof(path), "%s/%s/%s", tmpldir, osname, osver);
		if (stat(path, &st))
			continue;
		if (!S_ISDIR(st.st_mode))
			continue;

		/* test tok[i+2] */
		strcpy(extname, DEFSETNAME);
		j++;
		if (j < ntok) {
			/* is tok[i+2] arch or not */
			if (isarch(tok[j])) {
				osarch = tok[j];
				j++;
			}
			else
				osarch = defarch;

			if (j < ntok) {
				strcpy(extname, tok[j]);
				/* extname == rest */
				for (k=j+1; k<ntok; k++) {
					strcat(extname, "-");
					strcat(extname, tok[k]);
				}
			}
		}
		else
			osarch = defarch;

		snprintf(path, sizeof(path), "%s/%s/%s/%s/config/os/%s", \
			tmpldir, osname, osver, osarch, extname);

		if (strcmp(extname, DEFSETNAME) == 0) {
			if (is_base_os_tmpl(path)) {
				lfound = 1;
				break;
			}
		} else {
			if (is_tmpl(path)) {
				lfound = 1;
				break;
			}
		}
	}
	if ((!lfound) || (osver == NULL) || (osarch == NULL)) {
		vztt_logger(1, 0, "Can not find '%s' EZ os template", ostemplate);
		/* free all buffers */
		free(buf);
		free(osname);
		free(extname);
		free(tok);
		return VZT_TMPL_NOT_FOUND;
	}

	*pname = strdup(osname);
	*pver = strdup(osver);
	*parch = strdup(osarch);
	*pextraname = strdup(extname);
	if ((*pname == NULL) || (*pver == NULL) || \
			(*parch == NULL) || (*pextraname == NULL)) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	/* free all buffers */
	free(buf);
	free(osname);
	free(extname);
	free(tok);
	return 0;
}

/* load app templates for one appname from directory (base+extras)
and add into common list */
static int tmplset_init_apps(
		char *dirname,
		char *basename,
		struct app_tmpl_list *ls)
{
	char path[PATH_MAX+1];
	char buf[PATH_MAX+1];
	DIR * dir;
	char dirent_buf[sizeof(struct dirent) + PATH_MAX + 1];
	struct dirent *de = (struct dirent *) dirent_buf;
	struct dirent *result;
	int retval;
	int rc = 0;
	struct app_tmpl *app;
	struct app_tmpl *base = NULL;

	/* load base app template at the first */
	snprintf(path, sizeof(path), "%s/" DEFSETNAME, dirname);
	if (is_tmpl(path)) {
		if ((base = (struct app_tmpl *)\
				malloc(sizeof(struct app_tmpl))) == NULL) {
			vztt_logger(0, errno, "Cannot alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
		if ((rc = init_app_tmpl(base, basename, path, NULL)))
			return rc;
		if ((rc = app_tmpl_list_add(ls, base)))
			return rc;
	}

	if ((dir = opendir(dirname)) == NULL) {
		vztt_logger(0, errno, "opendir(\"%s\") error", dirname);
		return VZT_CANT_OPEN;
	}

	while (1) {
		if ((retval = readdir_r(dir, de, &result)) != 0)
		{
			vztt_logger(0, retval, "readdir_r(\"%s\") error",
					dirname);
			rc = VZT_CANT_READ;
			break;
		}

		if (result == NULL)
			break;

		if(strcmp(de->d_name, ".") == 0)
			continue;
		if(strcmp(de->d_name, "..") == 0)
			continue;
		if(strcmp(de->d_name, DEFSETNAME) == 0)
			continue;

		snprintf(path, sizeof(path), "%s/%s", dirname, de->d_name);
		if (!is_tmpl(path))
			continue;

		if ((app = (struct app_tmpl *)\
				malloc(sizeof(struct app_tmpl))) == NULL) {
			vztt_logger(0, errno, "Cannot alloc memory");
			rc = VZT_CANT_ALLOC_MEM;
			break;
		}
		snprintf(buf, sizeof(buf), "%s-%s", basename, de->d_name);
		if ((rc = init_app_tmpl(app, buf, path, base)))
			break;
		if ((rc = app_tmpl_list_add(ls, app)))
                {
                        /* TODO: free app */
			app_tmpl_clean(app);
			free(app);
			break;
                }
	}

	closedir(dir);
	return rc;
}

/* scan <tmpldir>/<basesubdir>/config/app directory and add OS template sets */
static int tmplset_init_all_apps(
		char *basedir,
		struct app_tmpl_list *ls)
{
	char dirname[PATH_MAX+1];
	char path[PATH_MAX+1];
	DIR * dir;
	char dirent_buf[sizeof(struct dirent) + PATH_MAX + 1];
	struct dirent *de = (struct dirent *) dirent_buf;
	struct dirent *result;
	int retval;
	struct stat st;
	int rc = 0;

	snprintf(dirname, sizeof(dirname), "%s/config/app", basedir);
	if (access(dirname, F_OK))
		return 0;

	if ((dir = opendir(dirname)) == NULL) {
		vztt_logger(0, errno, "opendir(\"%s\") error", dirname);
		return VZT_CANT_OPEN;
	}

	while (1) {
		if ((retval = readdir_r(dir, de, &result)) != 0)
		{
			vztt_logger(0, retval, "readdir_r(\"%s\") error",
					dirname);
			rc = VZT_CANT_READ;
			break;
		}

		if (result == NULL)
			break;

		if(strcmp(de->d_name, ".") == 0)
			continue;
		if(strcmp(de->d_name, "..") == 0)
			continue;

		snprintf(path, sizeof(path), "%s/%s", dirname, de->d_name);
		if (lstat(path, &st)) {
			vztt_logger(0, errno, "stat(\"%s\") error", path);
			rc = VZT_CANT_LSTAT;
			break;
		}
		if (!S_ISDIR(st.st_mode))
			continue;

		if ((rc = tmplset_init_apps(path, de->d_name, ls)))
			break;
	}

	closedir(dir);
	return rc;
}

/* load extra os templates and add into list */
static int tmplset_init_os(
		struct base_os_tmpl *base,
		struct os_tmpl_list *ls)
{
	int rc = 0;
	char dirname[PATH_MAX+1];
	char path[PATH_MAX+1];
	DIR * dir;
	char dirent_buf[sizeof(struct dirent) + PATH_MAX + 1];
	struct dirent *de = (struct dirent *) dirent_buf;
	struct dirent *result;
	int retval;
	struct os_tmpl *os;

	snprintf(dirname, sizeof(dirname), "%s/config/os", base->basedir);
	if ((dir = opendir(dirname)) == NULL) {
		vztt_logger(0, errno, "opendir(\"%s\") error", dirname);
		return VZT_CANT_OPEN;
	}

	while (1) {
		if ((retval = readdir_r(dir, de, &result)) != 0)
		{
			vztt_logger(0, retval, "readdir_r(\"%s\") error",
					dirname);
			rc = VZT_CANT_READ;
			break;
		}

		if (result == NULL)
			break;

		if(strcmp(de->d_name, ".") == 0)
			continue;
		if(strcmp(de->d_name, "..") == 0)
			continue;
		/* skip base OS tmpl */
		if(strcmp(de->d_name, DEFSETNAME) == 0)
			continue;

		snprintf(path, sizeof(path), "%s/%s", dirname, de->d_name);
		if (!is_tmpl(path))
			continue;

		if ((os = (struct os_tmpl *)\
				malloc(sizeof(struct os_tmpl))) == NULL) {
			vztt_logger(0, errno, "Cannot alloc memory");
			rc = VZT_CANT_ALLOC_MEM;
			break;
		}
		if ((rc = init_os_tmpl(os, path, de->d_name, base)))
			break;
		if ((rc = os_tmpl_list_add(ls, os)))
			break;
	}

	closedir(dir);
	return rc;
}

/*
 read base and extra os templates and apps templates names
*/
static int tmplset_init_template(
		char *tmpldir,
		char *osname,
		char *osver,
		char *osarch,
		char *extraname,
		int mask,
		int force,
		struct tmpl_set **tmpl)
{
	int rc;
	char path[PATH_MAX+1];

	if ((*tmpl = (struct tmpl_set *)\
			malloc(sizeof(struct tmpl_set))) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	/* initialization */
	(*tmpl)->base = NULL;
	(*tmpl)->os = NULL;
	os_tmpl_list_init(&(*tmpl)->oses);
	app_tmpl_list_init(&(*tmpl)->avail_apps);
	app_tmpl_list_init(&(*tmpl)->used_apps);
	(*tmpl)->mode = TMPLSET_DEF_MODE;

	if (((*tmpl)->base = (struct base_os_tmpl *)\
			malloc(sizeof(struct base_os_tmpl))) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	if ((rc = init_base_os_tmpl((*tmpl)->base, tmpldir, \
			osname, osver, osarch)))
		return rc;

	/* check technologies (ntpl) */
	if ((*tmpl)->base->technologies & VZ_T_NPTL && \
			vzctl2_check_tech(VZ_T_NPTL)) {
		vztt_logger(0, 0, "%s template require nptl technology "
			"but kernel does not support", (*tmpl)->os->name);
		return VZT_BAD_TECHNOLOGY;
	}

	/* set OS */
	if (strcmp(extraname, DEFSETNAME) == 0) {
		(*tmpl)->os = (struct os_tmpl *)(*tmpl)->base;
	} else {
		snprintf(path, sizeof(path), "%s/config/os/%s/packages", \
				(*tmpl)->base->basedir, extraname);
		if (access(path, F_OK)) {
			vztt_logger(1, 0, "Can not find %s-%s-%s-%s EZ os template",\
					osname, osver, osarch, extraname);
			return VZT_TMPL_NOT_FOUND;
		}
		if (((*tmpl)->os = (struct os_tmpl *)\
				malloc(sizeof(struct os_tmpl))) == NULL) {
			vztt_logger(0, errno, "Cannot alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
		snprintf(path, sizeof(path), "%s/config/os/%s", \
				(*tmpl)->base->basedir, extraname);
		if ((rc = init_os_tmpl((*tmpl)->os, path, \
				extraname, (*tmpl)->base)))
			return rc;
	}

	/* os templates list */
	if (mask & TMPLSET_LOAD_OS_LIST) {
		if ((rc = tmplset_init_os((*tmpl)->base, &(*tmpl)->oses)))
			return rc;
	}

	/* application templates processing */
	if (mask & TMPLSET_LOAD_APP_LIST) {
		/* load available application template list */
		if ((rc = tmplset_init_all_apps((*tmpl)->base->basedir, \
				&(*tmpl)->avail_apps)))
			return rc;
	}

	return 0;
}

/*
 load data from files for already initialized base os, extra os and application
*/
static int tmplset_load_template(
		struct tmpl_set *t,
		int mask,
		unsigned long fld_mask)
{
	int rc;
	struct os_tmpl_list_el *o;
	struct app_tmpl_list_el *a;

	if ((rc = load_base_os_tmpl(fld_mask, t->base)))
		return rc;

	/* set OS */
	if (t->os != (struct os_tmpl *)t->base) {
		if ((rc = load_os_tmpl(fld_mask, t->os)))
			return rc;
	}

	/* os templates list */
	if (mask & TMPLSET_LOAD_OS_LIST) {
		for (o = t->oses.tqh_first; o != NULL; o = o->e.tqe_next)
			if ((rc = load_os_tmpl(fld_mask, o->tmpl)))
				return rc;
	}

	/* load available application template list */
	if (mask & TMPLSET_LOAD_APP_LIST) {
		for (a = t->avail_apps.tqh_first; a != NULL; a = a->e.tqe_next)
			if ((rc = load_app_tmpl(fld_mask, a->tmpl)))
				return rc;
	}

	return 0;
}

int tmplset_install(
		struct tmpl_set *t,
		struct string_list *tmpls,
		char *ostemplate,
		int flags)
{
	char path[PATH_MAX+1];
	char tmpl_string[PATH_MAX+1];
	struct string_list_el *p;
	struct string_list pkgs;
	struct string_list pkgs_noarch;
	struct stat st;
	int rc = 0;

	string_list_init(&pkgs);
	string_list_init(&pkgs_noarch);

	if (t) {
		string_list_for_each(tmpls, p) {
			snprintf(tmpl_string, sizeof(tmpl_string),
					"%s-%s-%s-%s-ez", p->s, t->base->osname,
					t->base->osver, t->base->osarch);
			string_list_add(&pkgs, tmpl_string);
		}
	} else {
		snprintf(tmpl_string, sizeof(tmpl_string), \
				"%s-ez", *ostemplate == '.' ? ostemplate+1 : ostemplate);
		string_list_add(&pkgs, tmpl_string);
		snprintf(tmpl_string, sizeof(tmpl_string), \
				"%s-x86_64-ez", *ostemplate == '.' ? ostemplate+1 : ostemplate);
		string_list_add(&pkgs_noarch, tmpl_string);
	}

	vztt_logger(1, 0, "Some template(s): %s is not found, " \
	    "running " YUM " to install it...", tmpl_string);
	if ((rc = yum_install_execv_cmd(&pkgs, (flags & OPT_VZTT_QUIET), 1)) &&
		(rc = yum_install_execv_cmd(&pkgs_noarch, (flags & OPT_VZTT_QUIET), 1))) {
			vztt_logger(0, 0, "Failed to install the template(s):");
			#pragma GCC diagnostic ignored "-Waddress"
			string_list_for_each(&pkgs, p)
				vztt_logger(0, 0, "%s", p->s);
			string_list_for_each(&pkgs_noarch, p)
				vztt_logger(0, 0, "%s", p->s);
			goto cleanup;
	}

	/* Add app template(s) to the list */
	if (t) {
		string_list_for_each(tmpls, p) {
			snprintf(path, sizeof(path), "%s/config/app/%s",
			    t->base->basedir, p->s);
			if (lstat(path, &st)) {
				vztt_logger(0, errno, "stat(\"%s\") error",
				    path);
				rc = VZT_CANT_LSTAT;
				break;
			}

			if (!S_ISDIR(st.st_mode)) {
				rc = VZT_CANT_OPEN;
				goto cleanup;
			}

			if ((rc = tmplset_init_apps(path, p->s,
			    &t->avail_apps)))
				goto cleanup;
		}
	}

cleanup:
	string_list_clean(&pkgs);
	string_list_clean(&pkgs_noarch);
	return rc;
}

/*
 Check if precreated OVZ cache file exists in the cache directory
 If found, call external script to convert this cache into Vz7 one
 and create a dummy config for it
 */
int check_ovz_cache(
		char *tmpldir,
		char *ostemplate,
		int just_check)
{
	char cache_name[PATH_MAX+1];
	char *argv[] = {OVZ_CONVERT, cache_name, NULL};
	int rc;

	snprintf(cache_name, sizeof(cache_name), "%s/cache/%s.tar.gz", \
		tmpldir, ostemplate);

	if (just_check == 0)
		vztt_logger(1, 0, "Looking for the precreated template cache %s ",
				cache_name );

	if (access(cache_name, F_OK) == 0) {
		if (just_check)
			return 0;

		if ((rc = execv_cmd(argv, 0, 1))) {
			vztt_logger(0, 0, "Failed to convert the precreated cache %s",
						cache_name);
			return rc;
		}

		vztt_logger(1, 0, "The precreated cache %s for the container has been "
					"found and converted into the VZ7 format.\n"
					"Note: Such converted containers are not thoroughly "
					"tested and may have limited functionality. "
					"You are strongly recommended to use native "
					"Virtuozzo templates instead.",
					cache_name);
	}
	else
		return VZT_TMPL_NOT_CACHED;

	return 0;
}

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
		int flags)
{
	int rc, not_found = 0;
	char *osname = NULL, *osver = NULL, *osarch = NULL, *extraname = NULL;
	struct app_tmpl_list_el *a;
	struct string_list_el *p;
	struct string_list tmpls;

	/* parse template name */
	if ((rc = parse_os_name(tmpldir, ostemplate, \
			&osname, &osver, &osarch, &extraname))) {
		if ((flags & OPT_VZTT_USE_VZUP2DATE) &&
			rc == VZT_TMPL_NOT_FOUND) {
			if ((rc = tmplset_install(0, 0, ostemplate, flags))) {
				if (check_ovz_cache(tmpldir, ostemplate, 0) != 0)
					return rc;
			}
		} else {
			if (check_ovz_cache(tmpldir, ostemplate, 0) != 0)
				return rc;
		}
		if ((rc = parse_os_name(tmpldir, ostemplate,
			&osname, &osver, &osarch, &extraname)))
			return rc;
	}

	/* check template architecture */
	if ((rc = tmplset_check_arch(osarch)) && !(flags & OPT_VZTT_FORCE))
		return rc;

	if (apps) {
		if (!string_list_empty(apps))
			mask |= TMPLSET_LOAD_APP_LIST;
	}
	rc = tmplset_init_template(tmpldir, osname, osver, osarch, extraname, \
			mask, (flags & OPT_VZTT_FORCE), tmpl);
	free(osname);
	free(osver);
	free(osarch);
	free(extraname);
	if (rc)
		return rc;


	string_list_init(&tmpls);
	string_list_for_each(apps, p) {
		a = app_tmpl_list_find(&(*tmpl)->avail_apps, p->s);
		if (a == NULL) {
			not_found = 1;
			string_list_add(&tmpls, p->s);
		}
	}

	if ((flags & OPT_VZTT_USE_VZUP2DATE) && not_found && \
	    (rc = tmplset_install(*tmpl, &tmpls, 0, flags)))
		goto fail;

	// No error, but empty apps, skip the next code
	if (apps == NULL)
		goto fail;

        if ( flags & OPT_VZTT_AVAILABLE )
        {
            /* look for not installed apps */
            for( a = TAILQ_FIRST(&(*tmpl)->avail_apps); a != NULL; a = TAILQ_NEXT(a, e) )
            {	/* template is NOT in the list of installed */
                if(string_list_find( apps, a->tmpl->name ) == NULL)
                    if ((rc = app_tmpl_list_add(&(*tmpl)->used_apps, a->tmpl)))
                            goto fail;
            }
        } else
        {
            /* find available apps in <apps> and
            copy to used apps list in success*/
            string_list_for_each(apps, p) {
                    a = app_tmpl_list_find(&(*tmpl)->avail_apps, p->s);
                    if (a == NULL) {
                            vztt_logger(0, 0, "App template %s not found", p->s);
                            rc = VZT_TMPL_NOT_EXIST;
                            goto fail;
                    }
                    if ((rc = app_tmpl_list_add(&(*tmpl)->used_apps, a->tmpl)))
                            goto fail;
            }
        }
fail:
	string_list_clean(&tmpls);
	return rc;
}

/* clean template structure */
void tmplset_clean(struct tmpl_set *t)
{
	struct os_tmpl_list_el *o;
	struct app_tmpl_list_el *a;

	/* clean used structures */
	if (t->base)
	{
		base_os_tmpl_clean(t->base);
		free((void *)t->base);
		t->base = NULL;
	}

	for (a = t->avail_apps.tqh_first; a != NULL; a = a->e.tqe_next)
	{
		app_tmpl_clean(a->tmpl);
		free((void *)a->tmpl);
		a->tmpl = NULL;
	}
	for (o = t->oses.tqh_first; o != NULL; o = o->e.tqe_next)
	{
		os_tmpl_clean(o->tmpl);
		free((void *)o->tmpl);
		o->tmpl = NULL;
	}
	/* clean lists */
	app_tmpl_list_clean(&t->avail_apps);
	app_tmpl_list_clean(&t->used_apps); /* I hope that all templates from
	 this list were also in avail_apps list - and were freed there - bkb */
	os_tmpl_list_clean(&t->oses);
	free((void *)t);
}


/*
 read os and application templates datas for os template human name <ostemplate>
*/
int tmplset_load(
		char *tmpldir,
		char *ostemplate,
		struct string_list *apps,
		int mask,
		struct tmpl_set **tmpl,
		int flags)
{
	int rc;

	if ((rc = tmplset_init(tmpldir, ostemplate, apps, mask, tmpl, flags)))
		return rc;

	if ((rc = tmplset_load_template(*tmpl, mask, VZTT_INFO_TMPL_ALL)))
		return rc;

	return 0;
}

/*
 read os and application templates datas for os template human name <ostemplate>
 according fields mask <fld_mask>
*/
int tmplset_selective_load(
		char *tmpldir,
		char *ostemplate,
		struct string_list *apps,
		int mask,
		struct tmpl_set **tmpl,
		struct options_vztt *opts_vztt)
{
	int rc;

	if ((rc = tmplset_init(tmpldir, ostemplate, apps, mask, tmpl,
		opts_vztt->flags & ~OPT_VZTT_USE_VZUP2DATE)))
		return rc;

	if ((rc = tmplset_load_template(*tmpl, mask, opts_vztt->fld_mask)))
		return rc;

	return 0;
}

/* mark templates in <t> according names from <ls>
   use os, oses, avail_apps and used_apps fields according <mask>
   not found names will add into <nf>, if it is not NULL
 */
int tmplset_mark(
		struct tmpl_set *t,
		struct string_list *ls,
		int mask,
		struct string_list *nf)
{
	struct string_list_el *p;
	struct os_tmpl_list_el *o;
	struct app_tmpl_list_el *a;

	if (ls == NULL) {
		/* if ls is NULL and os template is in mask - mark it */
		if (mask & TMPLSET_MARK_OS)
			t->os->marker = 1;
		/* if ls is NULL and avail app templates is in mask -
		mark all avail apps */
		if (mask & TMPLSET_MARK_AVAIL_APP_LIST) {
			app_tmpl_list_for_each(&t->avail_apps, a)
				a->tmpl->marker = 1;
		}
		/* the same for os used list */
		if (mask & TMPLSET_MARK_USED_APP_LIST) {
			app_tmpl_list_for_each(&t->used_apps, a)
				a->tmpl->marker = 1;
		}
		/* the same for os template list */
		if (mask & TMPLSET_MARK_OS_LIST) {
			os_tmpl_list_for_each(&t->oses, o)
				o->tmpl->marker = 1;
		}
		return 0;
	}

	string_list_for_each(ls, p) {
		if (mask & TMPLSET_MARK_OS) {
			if (strcmp(t->os->name, p->s) == 0)
				t->os->marker = 1;
		}
		if (mask & TMPLSET_MARK_OS_LIST) {
			if ((o = os_tmpl_list_find(&t->oses, p->s))) {
				o->tmpl->marker = 1;
				continue;
			}
		}
		if (mask & TMPLSET_MARK_AVAIL_APP_LIST) {
			if ((a = app_tmpl_list_find(&t->avail_apps, p->s))) {
				a->tmpl->marker = 1;
				continue;
			}
		}
		if (mask & TMPLSET_MARK_USED_APP_LIST) {
			if ((a = app_tmpl_list_find(&t->used_apps, p->s))) {
				a->tmpl->marker = 1;
				continue;
			}
		}
		/* tmpl not found - what to 'not found' list */
		if (nf)
			string_list_add(nf, p->s);
	}
	return 0;
}

/* unmark all templates */
int tmplset_unmark_all(struct tmpl_set *t)
{
	struct os_tmpl_list_el *o;
	struct app_tmpl_list_el *a;

	t->base->marker = 0;
	t->os->marker = 0;
	for (a = t->avail_apps.tqh_first; a != NULL; a = a->e.tqe_next)
		a->tmpl->marker = 0;
	for (o = t->oses.tqh_first; o != NULL; o = o->e.tqe_next)
		o->tmpl->marker = 0;
	return 0;
}

/* get os template packages list */
static int tmplset_get_os_pkgs(
		struct os_tmpl *os,
		struct string_list *packages)
{
	int rc;
	/* only packages0 can content some packages in one string,
	separated by space (by desing, for debian only),
	and so split every string to separate packages */
	struct string_list_el *p;
	char buf[PATH_MAX];
	char *str, *tok;
	char *saveptr;

	for (p = os->packages0.tqh_first; p != NULL; p = p->e.tqe_next) {
		strncpy(buf, p->s, sizeof(buf));
		for (str = buf; ;str = NULL) {
			tok = strtok_r(str, " 	", &saveptr);
			if (tok == NULL)
				break;
			string_list_add(packages, tok);
		}
	}
	if ((rc = string_list_copy(packages, &os->packages1)))
		return rc;
	if ((rc = string_list_copy(packages, &os->packages)))
		return rc;

	return 0;
}

/* get common packages list */
static int tmplset_get_pkgs(
		struct tmpl_set *t,
		int marker,
		struct string_list *packages)
{
	int rc;
	struct os_tmpl_list_el *o;
	struct app_tmpl_list_el *a;

	if (t->os && t->os->marker == marker) {
		if ((rc = tmplset_get_os_pkgs(t->os, packages)))
			return rc;
	}
	for (o = t->oses.tqh_first; o != NULL; o = o->e.tqe_next) {
		if (o->tmpl->marker == marker) {
			if ((rc = tmplset_get_os_pkgs(o->tmpl, packages)))
				return rc;
		}
	}
	for (a = t->avail_apps.tqh_first; a != NULL; a = a->e.tqe_next) {
		if (a->tmpl->marker == marker) {
			if ((rc = string_list_copy(packages, \
					&a->tmpl->packages)))
				return rc;
		}
	}
/*
It's superfluous, because of used_apps is subset of avail_apps (used
pointers to structs from avail_apps list) and at really list will dublicated
	for (a = t->used_apps.tqh_first; a != NULL; a = a->e.tqe_next)
		if (a->tmpl->marker == marker)
			copy_string(packages, &a->tmpl->packages);
*/
	return 0;
}

/* get common packages list for marked templates */
int tmplset_get_marked_pkgs(struct tmpl_set *t, struct string_list *packages)
{
	return tmplset_get_pkgs(t, 1, packages);
}

/* get common packages list for non VE marked templates */
int tmplset_get_ve_nonmarked_pkgs(
		struct tmpl_set *t,
		struct string_list *packages)
{
	int rc;
	struct app_tmpl_list_el *a;

	if (t->os && !t->os->marker) {
		/* only packages0 can content some packages in one string,
		separated by space (by desing, for debian only),
		and so split every string to separate packages */
		struct string_list_el *p;
		char buf[PATH_MAX];
		char *str, *tok;
		char *saveptr;

		for (p = t->os->packages0.tqh_first; p != NULL; \
				p = p->e.tqe_next) {
			strncpy(buf, p->s, sizeof(buf));
			for (str = buf; ;str = NULL) {
				tok = strtok_r(str, " 	", &saveptr);
				if (tok == NULL)
					break;
				string_list_add(packages, tok);
			}
		}
		if ((rc = string_list_copy(packages, &t->os->packages1)))
			return rc;
		if ((rc = string_list_copy(packages, &t->os->packages)))
			return rc;
	}
	for (a = t->used_apps.tqh_first; a != NULL; a = a->e.tqe_next) {
		if (!a->tmpl->marker) {
			if ((rc = string_list_copy(packages, \
					&a->tmpl->packages)))
				return rc;
		}
	}
	return 0;
}

/* get repositories & mirrorlists for marked templates */
int tmplset_get_urls(
		struct tmpl_set *t,
		struct repo_list *repositories,
		struct repo_list *zypp_repositories,
		struct repo_list *mirrorlist)
{
	struct os_tmpl_list_el *o;
	struct app_tmpl_list_el *a;

	for (a = t->avail_apps.tqh_first; a != NULL; a = a->e.tqe_next) {
		if (!a->tmpl->marker)
			continue;
		copy_url(repositories, &a->tmpl->repositories);
		copy_url(zypp_repositories, &a->tmpl->zypp_repositories);
		copy_url(mirrorlist, &a->tmpl->mirrorlist);
	}
	for (a = t->used_apps.tqh_first; a != NULL; a = a->e.tqe_next) {
		if (!a->tmpl->marker)
			continue;
		copy_url(repositories, &a->tmpl->repositories);
		copy_url(zypp_repositories, &a->tmpl->zypp_repositories);
		copy_url(mirrorlist, &a->tmpl->mirrorlist);
	}
	for (o = t->oses.tqh_first; o != NULL; o = o->e.tqe_next) {
		if (!o->tmpl->marker)
			continue;
		copy_url(repositories, &o->tmpl->repositories);
		copy_url(zypp_repositories, &o->tmpl->zypp_repositories);
		copy_url(mirrorlist, &o->tmpl->mirrorlist);
	}
	if (t->os) {
		/* always add OS template repos ignoring marker
		except for "separate repo" mode (used for update metadata) */
		if (!(t->mode & TMPLSET_SEPARATE_REPO_MODE) || t->os->marker) {
			copy_url(repositories, &t->os->repositories);
			copy_url(zypp_repositories, &t->os->zypp_repositories);
			copy_url(mirrorlist, &t->os->mirrorlist);
		}
	}

	if (t->base) {
		/* always add base OS template repos ignoring marker
		except for "separate repo" mode (used for update metadata) */
		if (!(t->mode & TMPLSET_SEPARATE_REPO_MODE) ||
				t->base->marker || t->os != (struct os_tmpl *) t->base) {
			copy_url(repositories, &t->base->repositories);
			copy_url(zypp_repositories, &t->base->zypp_repositories);
			copy_url(mirrorlist, &t->base->mirrorlist);
		}
	}
/* TODO: check section names - if already used? */

	if (repo_list_empty(repositories) && repo_list_empty(mirrorlist))
		return VZT_TMPL_BROKEN;

	return 0;
}

/* get names of marked templates with own repositories or mirrorlists,
exclude base os template. used for ve or cache only */
int tmplset_get_repos(
		struct tmpl_set *t,
		struct string_list *ls)
{
	int rc;
	struct app_tmpl_list_el *a;

	if ((rc = string_list_add(ls, t->base->name)))
		return rc;
	if (t->os->marker && (t->os != (struct os_tmpl *)t->base)) {
		if (!repo_list_empty(&t->os->repositories) || \
				!repo_list_empty(&t->os->zypp_repositories) || \
				!repo_list_empty(&t->os->mirrorlist)) {
			if ((rc = string_list_add(ls, t->os->name)))
				return rc;
		}
	}

	for (a = t->used_apps.tqh_first; a != NULL; a = a->e.tqe_next) {
		if (!a->tmpl->marker)
			continue;
		if (repo_list_empty(&a->tmpl->repositories) && \
				repo_list_empty(&a->tmpl->zypp_repositories) && \
				repo_list_empty(&a->tmpl->mirrorlist))
			continue;
		if ((rc = string_list_add(ls, a->tmpl->name)))
			return rc;
	}
	return 0;
}

/* get environments: if it does not defined in os, use base environment
   This function does not copy envs struct, but return pointer only */
struct string_list *tmplset_get_envs(struct tmpl_set *t)
{
	if (!string_list_empty(&t->os->environment))
		return &t->os->environment;

	return &t->base->environment;
}

/* get template info */
int tmplset_get_info(
		struct global_config *gc,
		struct tmpl_set *t,
		struct url_map_list *url_map,
		struct tmpl_info *info)
{
	int rc = 0;
	struct app_tmpl_list_el *a;

	for (a = t->avail_apps.tqh_first; a != NULL; a = a->e.tqe_next) {
		if (!a->tmpl->marker)
			continue;
		rc = app_tmpl_get_info(a->tmpl, url_map, info);
		return rc;
	}
	if (t->os->marker)
		rc = os_tmpl_get_info(gc, t->os, t->base, url_map, info);

	return rc;
}

/* update timestamp file in VE private area for marked templates */
int tmplset_update_privdir(
		struct tmpl_set *t,
		char *ve_private)
{
	struct os_tmpl_list_el *o;
	struct app_tmpl_list_el *a;

	for (a = t->avail_apps.tqh_first; a != NULL; a = a->e.tqe_next) {
		if (a->tmpl->marker)
			update_tmpl_privdir(ve_private, a->tmpl->name);
	}
	for (a = t->used_apps.tqh_first; a != NULL; a = a->e.tqe_next) {
		if (a->tmpl->marker)
			update_tmpl_privdir(ve_private, a->tmpl->name);
	}
	for (o = t->oses.tqh_first; o != NULL; o = o->e.tqe_next) {
		if (o->tmpl->marker)
			update_tmpl_privdir(ve_private, o->tmpl->name);
	}
	if (t->os->marker)
		update_tmpl_privdir(ve_private, t->os->name);
	return 0;
}

/* TODO: clear_tmplset() */

/* run scripts for marked templates (exclude oses)
   application scripts errors will ignore
   TODO: force option */
int tmplset_run_ve_scripts(
		struct tmpl_set *t,
		const char *ctid,
		char *ve_root,
		char *script,
		struct string_list *environment,
		int progress_fd)
{
	int rc;
	struct app_tmpl_list_el *a;

	for (a = t->avail_apps.tqh_first; a != NULL; a = a->e.tqe_next) {
		if (!a->tmpl->marker)
			continue;
		if ((rc = run_ve_scripts((struct tmpl *)a->tmpl, ctid, \
			ve_root, script, environment, progress_fd)))
			return rc;
	}
	if (t->os && t->os->marker) {
		if ((rc = run_ve_scripts((struct tmpl *)t->os, \
			ctid, ve_root, script, environment, progress_fd)))
			return rc;
	}

	return 0;
}

/* Call template scripts */
int tmplset_run_ve0_scripts(
		struct tmpl_set *t,
		char *ve_root,
		const char *ctid,
		char *script,
		struct string_list *environment,
		int progress_fd)
{
	int rc = 0;
	struct app_tmpl_list_el *a;

	for (a = t->avail_apps.tqh_first; a != NULL; a = a->e.tqe_next) {
		if (!a->tmpl->marker)
			continue;
		if ((rc = run_ve0_scripts((struct os_tmpl *)a->tmpl, \
			ve_root, ctid, script, environment, progress_fd)))
			return rc;
	}
	if (t->os && t->os->marker) {
		rc = run_ve0_scripts((struct os_tmpl *)t->os, \
			ve_root, ctid, script, environment, progress_fd);
	}

	return rc;
}




/* remove from <dst> all records, which will found in <src> */
static int cutout_pkgset(
		struct Transaction *t,
		struct package_list *dst,
		struct string_list *src)
{
	struct package_list_el *p;
	struct string_list_el *s;

	for (s = src->tqh_first; s != NULL; s = s->e.tqe_next) {
		for (p = dst->tqh_first; p != NULL; p = p->e.tqe_next) {
			if (t->pm_pkg_cmp(s->s, p->p) == 0) {
				package_list_remove(dst, p);
				break;
			}
		}
	}

	return 0;
}

/* to find all packages from <src> set in <dst> set and return 0 in success */
static int find_whole_pkgset(
		struct Transaction *t,
		struct package_list *dst,
		struct string_list *src)
{
	struct package_list_el *d;
	struct string_list_el *s;
	int lfound;

	string_list_for_each(src, s) {
		lfound = 0;
		package_list_for_each(dst, d) {
			if (t->pm_pkg_cmp(s->s, d->p) == 0) {
				d->p->marker = 1;
				lfound = 1;
				break;
			}
		}
		if (!lfound)
			return 1;
	}

	return 0;
}

/* to find all packages <src> in <dst> and cut them from <dst> if found
   return 0 in success */
static int cutout_whole_pkgset(
		struct Transaction *t,
		struct package_list *dst,
		struct string_list *src)
{
	struct package_list_el *d;

	/* clear markers */
	package_list_for_each(dst, d) {
		if (d->p->marker)
			d->p->marker = 0;
	}

	if (find_whole_pkgset(t, dst, src))
		return 1;

	/* remove marked packages from dst */
	package_list_for_each(dst, d) {
		if (d->p->marker)
			d = package_list_remove(dst, d);
	}

	return 0;
}


/* templates are crosses packages sets, therefore
we should check all installed templates in packages list
before this list cleaning


we have package set for VE <packages> and  */
static int remove_packages(
		struct tmpl_set *t,
		struct Transaction *trans,
		char *ve_private,
		struct package_list *packages)
{
	struct app_tmpl_list_el *a;

	for (a = t->used_apps.tqh_first; a != NULL; a = a->e.tqe_next) {
		if (find_whole_pkgset(trans, packages, &a->tmpl->packages)) {
			/* not found */
			/* this template was implicitly removed
			   to remove directory for this template from private */
			remove_tmpl_privdir(ve_private, a->tmpl->name);
			/* now previous will current */
			a = app_tmpl_list_remove(&t->used_apps, a);
		}
	}
	return 0;
}

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
		struct string_list *apps)
{
	struct app_tmpl_list_el *a;

	/* clean packages list from installed templates*/
	/* remove os template packages from list */
	cutout_pkgset(trans, packages, &t->os->packages0);
	cutout_pkgset(trans, packages, &t->os->packages1);
	cutout_pkgset(trans, packages, &t->os->packages);
	/* and the same for all used applications */
	for (a = t->used_apps.tqh_first; a != NULL; a = a->e.tqe_next) {
		cutout_pkgset(trans, packages, &a->tmpl->packages);
		if (string_list_find(apps, a->tmpl->name) == NULL)
			string_list_add(apps, a->tmpl->name);
	}

	/* get app templates list from remains packages list */
	/* find in default set at the first??? */
	for (a = t->avail_apps.tqh_first; a != NULL; a = a->e.tqe_next) {
		/* Skip app templates with empty lists: #PSBM-26883 */
		if (string_list_empty(&a->tmpl->packages))
			continue;
		if (cutout_whole_pkgset(trans, packages,
				&a->tmpl->packages) == 0) {
			if (string_list_find(apps, a->tmpl->name) == NULL)
				string_list_add(apps, a->tmpl->name);
			update_tmpl_privdir(ve_private, a->tmpl->name);
		}
	}

	return 0;
}

/* add marked app templates from avail_app to used_apps list */
int tmplset_add_marked_apps(
		struct tmpl_set *t,
		char *ve_private,
		struct string_list *apps)
{
	struct app_tmpl_list_el *a;

	/* add marked available in installed */
	for (a = t->avail_apps.tqh_first; a != NULL; a = a->e.tqe_next) {
		if (a->tmpl->marker) {
			app_tmpl_list_add(&t->used_apps, a->tmpl);
			update_tmpl_privdir(ve_private, a->tmpl->name);
		}
	}

	/* and add all installed templates in list */
	for (a = t->used_apps.tqh_first; a != NULL; a = a->e.tqe_next) {
		if (string_list_find(apps, a->tmpl->name) == NULL)
			string_list_add(apps, a->tmpl->name);
	}

	return 0;
}

/* add marked app templates from avail_app to used_apps list
   and check indirectly installed templates */
int tmplset_add_marked_and_check_apps(
		struct tmpl_set *t,
		struct Transaction *trans,
		char *ve_private,
		struct package_list *pkgs,
		struct string_list *existed)
{
	struct app_tmpl_list_el *a;

	/* check installed templates */
	remove_packages(t, trans, ve_private, pkgs);

	/* seek in available */
	for (a = t->avail_apps.tqh_first; a != NULL; a = a->e.tqe_next) {
		if (a->tmpl->marker) {
			/* add to used app tmpl list */
			app_tmpl_list_add(&t->used_apps, a->tmpl);
			update_tmpl_privdir(ve_private, a->tmpl->name);
		}
	}

	/* check indirectly installed templates */
	tmplset_get_installed_apps(t, trans, ve_private, pkgs, existed);

	return 0;
}

/* remove app templates from used_apps list
   do not check nothing - only find and move */
int tmplset_remove_marked_apps(
		struct tmpl_set *t,
		char *ve_private,
		struct string_list *apps)
{
	struct app_tmpl_list_el *a;

	/* seek in used */
	for (a = t->used_apps.tqh_first; a != NULL; a = a->e.tqe_next) {
		if (a->tmpl->marker) {
			/* remove from used */
			remove_tmpl_privdir(ve_private, a->tmpl->name);
			/* now previous will current */
			a = app_tmpl_list_remove(&t->used_apps, a);
		} else {
			if (string_list_find(apps, a->tmpl->name) == NULL)
				string_list_add(apps, a->tmpl->name);
		}
	}

	return 0;
}

/* remove app templates from used_apps list
   and check indirectly removed templates */
int tmplset_remove_marked_and_check_apps(
		struct tmpl_set *t,
		struct Transaction *trans,
		char *ve_private,
		struct package_list *pkgs,
		struct string_list *existed)
{
	struct app_tmpl_list_el *a;

	/* seek in used */
	for (a = t->used_apps.tqh_first; a != NULL; a = a->e.tqe_next) {
		if (a->tmpl->marker) {
			/* remove from used */
			remove_tmpl_privdir(ve_private, a->tmpl->name);
			/* now previous will current */
			a = app_tmpl_list_remove(&t->used_apps, a);
		}
	}

	/* check installed templates */
	remove_packages(t, trans, ve_private, pkgs);

	/* check indirectly installed templates */
	tmplset_get_installed_apps(t, trans, ve_private, pkgs, existed);

	return 0;
}

/* check installed templates after packages install/remove */
int tmplset_check_apps(
		struct tmpl_set *t,
		struct Transaction *trans,
		char *ve_private,
		struct package_list *pkgs,
		struct string_list *existed)
{
	/* check installed templates */
	remove_packages(t, trans, ve_private, pkgs);

	/* check indirectly installed templates */
	tmplset_get_installed_apps(t, trans, ve_private, pkgs, existed);

	return 0;
}

/* now try to find ostemplate with osname & osarch and
   upgradable_version == osver or osver = osver+1 */
/* to sort available versions as rpm versions in descending order */
static int find_upgrade_version(
	struct base_os_tmpl *base,
	char *version,
	size_t size)
{
	int rc = 0;
	char dirname[PATH_MAX+1];
	char buf[PATH_MAX+1];
	DIR * dir;
	char dirent_buf[sizeof(struct dirent) + PATH_MAX + 1];
	struct dirent *de = (struct dirent *) dirent_buf;
	struct dirent *result;
	int retval;
	struct string_list upgradable_versions;
	struct string_list_el *u;
	char *ptr;
	int ldig = 1;
	char *uver = NULL;

	string_list_init(&upgradable_versions);

	/* is osver a number ? */
	for (ptr = base->osver; *ptr; ++ptr) {
		if (!isdigit(*ptr)) {
			ldig = 0;
			break;
		}
	}
	if (ldig) {
		/* osver is a number, will suppose that upgrade
		version == osver + 1 by default */
		snprintf(buf, sizeof(buf), "%d", atoi(base->osver) + 1);
		if ((uver = strdup(buf)) == NULL) {
			vztt_logger(0, errno, "Cannot alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	}

	snprintf(dirname, sizeof(dirname), "%s/%s", \
			base->tmpldir, base->osname);
	if ((dir = opendir(dirname)) == NULL) {
		vztt_logger(0, errno, "opendir(\"%s\") error", dirname);
		return VZT_CANT_OPEN;
	}

	while (1) {
		if ((retval = readdir_r(dir, de, &result)) != 0)
		{
			vztt_logger(0, retval, "readdir_r(\"%s\") error",
					dirname);
			rc = VZT_CANT_READ;
			break;
		}

		if (result == NULL)
			break;

		if(!strcmp(de->d_name, "."))
			continue;
		if(!strcmp(de->d_name, ".."))
			continue;
		if(strcmp(de->d_name, base->osver) == 0)
			continue;

		/* is it EZ base OS template ? */
		snprintf(buf, sizeof(buf), \
			"%s/%s/%s/config/os/" DEFSETNAME,\
			dirname, de->d_name, base->osarch);
		if (!is_base_os_tmpl(buf))
			continue;

		snprintf(buf, sizeof(buf), \
			"%s/%s/%s/config/os/" DEFSETNAME \
			"/upgradable_versions", \
			dirname, de->d_name, base->osarch);
		if (access(buf, F_OK) == 0) {
			/* check upgradable_versions at the first */
			string_list_read(buf, &upgradable_versions);
			u = string_list_find(&upgradable_versions, \
				base->osver);
			string_list_clean(&upgradable_versions);
			if (u) {
				strncpy(version, de->d_name, size);
/* TODO: select newest */
				break;
			}
		}
		/* check osver+1 variant */
		if (uver) {
			if (strcmp(uver, de->d_name) == 0) {
				strncpy(version, de->d_name, size);
				break;
			}
		}
	}
	closedir(dir);
	if (uver)
		free((void *)uver);

	return rc;
}

/* find, load & mark update template set <dst> for template set <src> */
int tmplset_find_upgrade(
		struct tmpl_set *src,
		int force,
		int packages,
		struct tmpl_set **dst)
{
	int rc = 0;
	char buf[PATH_MAX+1];
	struct app_tmpl_list_el *s, *d;

	buf[0] = '\0';
	if ((rc = find_upgrade_version(src->base, buf, sizeof(buf))))
		return 0;

	if (strlen(buf) == 0) {
		error(0, 0, "Cannot find upgrade for %s OS template", \
				src->base->name);
		return VZT_CANT_OPEN;
	}

	/* To use src->os->setname instead of DEFSETNAME ? */
	if ((rc = tmplset_init_template(src->base->tmpldir, src->base->osname,\
			buf, src->base->osarch, DEFSETNAME, \
			TMPLSET_LOAD_APP_LIST, force, dst)))
		return rc;

	/* load target applications */
	app_tmpl_list_for_each(&src->used_apps, s) {
		if (repo_list_empty(&s->tmpl->repositories) && \
				repo_list_empty(&s->tmpl->zypp_repositories) && \
				repo_list_empty(&s->tmpl->mirrorlist) && \
				packages) {
			/* skip app without repositories/mirrorlists
			   in packages mode */
			continue;
		}
		d = app_tmpl_list_find(&(*dst)->avail_apps, s->tmpl->name);
		if (d != NULL) {
			rc = app_tmpl_list_add(&(*dst)->used_apps, d->tmpl);
			if (rc)
				return rc;
			continue;
		}
		if ((!repo_list_empty(&s->tmpl->repositories) || \
				!repo_list_empty(&s->tmpl->zypp_repositories) || \
				!repo_list_empty(&s->tmpl->mirrorlist)) && \
				!force) {
			/* failed only for app with repositories/mirrorlists
			   without force option */
			vztt_logger(0, 0,
				"App template %s not found for target OS %s",
				s->tmpl->name, (*dst)->base->name);
			return VZT_TMPL_NOT_EXIST;
		}
	}

	if ((rc = tmplset_load_template(*dst, TMPLSET_LOAD_APP_LIST,
			VZTT_INFO_TMPL_ALL)))
		return rc;

	return 0;
}

/* check template from list <tl> are available */
int tmplset_check_find(
		struct tmpl_set *t,
		struct string_list *tl,
		struct string_list *tmpls)
{
	int rc, lfound;
	struct app_tmpl_list_el *a;
	struct string_list_el *s;

	rc = 0;
	for (s = tl->tqh_first; s != NULL; s = s->e.tqe_next) {
		lfound = 0;
		if (t->os) {
			if (strcmp(t->os->name, s->s) == 0)
				continue;
		}
		for (a = t->avail_apps.tqh_first; a != NULL; a = a->e.tqe_next){
			if (strcmp(a->tmpl->name, s->s) == 0) {
				lfound = 1;
				break;
			}
		}
		if (!lfound) {
			string_list_add(tmpls, s->s);
			if (rc == 0)
				vztt_logger(0, 0, "Application template(s) "\
					"does not exist:");
			vztt_logger(0, 0, "\t%s", s->s);
			rc = VZT_TMPL_NOT_EXIST;
		}
	}
	return rc;
}

/* check template from list <tl> are available */
int tmplset_check(
		struct tmpl_set *t,
		struct string_list *tl,
		int flags)
{
	int rc;
	struct string_list tmpls;
	struct app_tmpl_list_el *a;
	struct string_list_el *p;
	unsigned long fld_mask;

	string_list_init(&tmpls);

	if ((rc = tmplset_check_find(t, tl, &tmpls)) == VZT_TMPL_NOT_EXIST) {
		if ((flags & OPT_VZTT_USE_VZUP2DATE) && \
		    !(rc = tmplset_install(t, &tmpls, 0, flags))) {
			fld_mask = VZTT_INFO_PACKAGES | VZTT_INFO_MIRRORLIST |
			    VZTT_INFO_REPOSITORIES | VZTT_INFO_DESCRIPTION |
			    VZTT_INFO_SUMMARY;

			#pragma GCC diagnostic ignored "-Waddress"
			string_list_for_each(&tmpls, p) {
				for (a = t->avail_apps.tqh_first; a != NULL;
				    a = a->e.tqe_next) {
					if (!strcmp(a->tmpl->name, p->s)) {
						if ((rc = load_app_tmpl(
						fld_mask, a->tmpl)))
							return rc;
					}
				}
			}
			rc = tmplset_check_find(t, tl, &tmpls);
		}
	}

	string_list_clean(&tmpls);

	return rc;
}

/* check that marked os and app templates are installed */
int tmplset_check_for_update(
		struct tmpl_set *t,
		struct string_list *tl,
		int flags)
{
	int rc;
	struct string_list_el *s;

	if ((rc = tmplset_check(t, tl, flags)))
		return rc;

	rc = 0;
	for (s = tl->tqh_first; s != NULL; s = s->e.tqe_next) {
		if (t->os) {
			if (strcmp(t->os->name, s->s) == 0)
				continue;
		}
		if (app_tmpl_list_find(&t->used_apps, s->s) == NULL) {
			if (rc == 0)
				vztt_logger(0, 0, "Template(s) "\
					"does not installed into CT:");
			vztt_logger(0, 0, "\t%s", s->s);
			rc = VZT_TMPL_NOT_INSTALLED;
		}
	}

	return (flags & OPT_VZTT_FORCE) ? 0 : rc;
}

/* check that marked app templates are installed */
int tmplset_check_for_remove(
		struct tmpl_set *t,
		struct string_list *apps,
		int flags)
{
	int rc, lfound;
	struct app_tmpl_list_el *a;
	struct string_list_el *s;

	if ((rc = tmplset_check(t, apps, flags)))
		return rc;

	rc = 0;
	for (s = apps->tqh_first; s != NULL; s = s->e.tqe_next) {
		lfound = 0;
		for (a = t->used_apps.tqh_first; a != NULL; a = a->e.tqe_next){
			if (strcmp(a->tmpl->name, s->s) == 0) {
				lfound = 1;
				break;
			}
		}
		if (!lfound) {
			if (rc == 0)
				vztt_logger(0, 0, "Application template(s) "\
					"does not installed into CT:");
			vztt_logger(0, 0, "\t%s", s->s);
			rc = VZT_TMPL_NOT_INSTALLED;
		}
	}

	return (flags & OPT_VZTT_FORCE) ? 0 : rc;
}

/* check that marked app templates does not installed yet */
int tmplset_check_for_install(
		struct tmpl_set *t,
		struct string_list *apps,
		int flags)
{
	int rc, lfound;
	struct app_tmpl_list_el *a;
	struct string_list_el *s;

	if ((rc = tmplset_check(t, apps, flags)))
		return rc;

	rc = 0;
	for (s = apps->tqh_first; s != NULL; s = s->e.tqe_next) {
		lfound = 0;
		for (a = t->used_apps.tqh_first; a != NULL; a = a->e.tqe_next){
			if (strcmp(a->tmpl->name, s->s) == 0) {
				lfound = 1;
				break;
			}
		}
		if (lfound) {
			if (rc == 0)
				vztt_logger(0, 0, "Application template(s) already "\
					"installed:");
			vztt_logger(0, 0, "\t%s", s->s);
			rc = VZT_TMPL_INSTALLED;
		}
	}

	return (flags & OPT_VZTT_FORCE) ?  0 : rc;
}

/* get list of base and extra OS templates */
int tmplset_get_os_names(struct tmpl_set *t, struct string_list *names)
{
	struct os_tmpl_list_el *o;

	string_list_add(names, t->base->name);
	for (o = t->oses.tqh_first; o != NULL; o = o->e.tqe_next)
		string_list_add(names, o->tmpl->name);

        return 0;
}

/* seek base OS templates in /vz/template/<osname> directory */
static int tmplset_seek_base(
		char *dirname,
		char *osname,
		struct string_list *ls)
{
	char buf[PATH_MAX+1];
	DIR * dir;
	char dirent_buf[sizeof(struct dirent) + PATH_MAX + 1];
	struct dirent *de = (struct dirent *) dirent_buf;
	struct dirent *result;
	int retval;
	struct stat st;
	int rc = 0, i;
	char **archs = get_available_archs();

	if ((dir = opendir(dirname)) == NULL) {
		vztt_logger(0, errno, "opendir(\"%s\") error", dirname);
		return VZT_CANT_OPEN;
	}

	while (1) {
		if ((retval = readdir_r(dir, de, &result)) != 0)
		{
			vztt_logger(0, retval, "readdir_r(\"%s\") error",
					dirname);
			rc = VZT_CANT_READ;
			break;
		}

		if (result == NULL)
			break;

		if(strcmp(de->d_name, ".") == 0)
			continue;
		if(strcmp(de->d_name, "..") == 0)
			continue;

		snprintf(buf, sizeof(buf), "%s/%s", dirname, de->d_name);
		if (lstat(buf, &st)) {
			vztt_logger(0, errno, "stat(\"%s\") error", buf);
			continue;
		}
		if (!S_ISDIR(st.st_mode))
			continue;

		/* seek all available template architectures */
		for (i = 0; archs[i]; i++) {
			snprintf(buf, sizeof(buf), \
				"%s/%s/%s/config/os/" DEFSETNAME, \
				dirname, de->d_name, archs[i]);
			if (is_base_os_tmpl(buf)) {
				snprintf(buf, sizeof(buf), "%s-%s-%s", \
					osname, de->d_name, archs[i]);
				string_list_add(ls, buf);
			}
		}
	}
	closedir(dir);

	return rc;
}

/* seek all base OS templates in <tmpldir> directory */
int tmplset_get_all_base(
		char *tmpldir,
		struct string_list *ls)
{
	char buf[PATH_MAX+1];
	char path[PATH_MAX+1];
	DIR * dir;
	char dirent_buf[sizeof(struct dirent) + PATH_MAX + 1];
	struct dirent *de = (struct dirent *) dirent_buf;
	struct dirent *result;
	int retval;
	struct stat st;
	int rc = 0;

	if ((dir = opendir(tmpldir)) == NULL) {
		if (errno != ENOENT) {
			vztt_logger(0, errno, "opendir(\"%s\") error", tmpldir);
			return VZT_CANT_OPEN;
		} else {
			/* skip ENOENT (#116418) */
			vztt_logger(1, 0, "Warning: template area directory %s "\
				"does not exist", tmpldir);
			return 0;
		}
	}

	while (1) {
		if ((retval = readdir_r(dir, de, &result)) != 0)
		{
			vztt_logger(0, retval, "readdir_r(\"%s\") error",
					tmpldir);
			rc = VZT_CANT_READ;
			break;
		}

		if (result == NULL)
			break;

		if(strcmp(de->d_name, ".") == 0)
			continue;
		if(strcmp(de->d_name, "..") == 0)
			continue;

		snprintf(path, sizeof(path), "%s/%s", tmpldir, de->d_name);
		if (lstat(path, &st)) {
			vztt_logger(0, errno, "stat(\"%s\") error", path);
			continue;
		}
		if (!S_ISDIR(st.st_mode))
			continue;

		/* is it standard template directory?
		  seek /vz/template/<template name>/conf directory */
		snprintf(buf, sizeof(buf), "%s/%s/conf", tmpldir, de->d_name);
		if (lstat(buf, &st) ==0 ) {
			if (S_ISDIR(st.st_mode))
				continue;
		}

		snprintf(path, sizeof(path), "%s/%s", tmpldir, de->d_name);
		if ((rc = tmplset_seek_base(path, de->d_name, ls)))
			break;
	}
	closedir(dir);

	return rc;
}

/* alloc template array */
int tmplset_alloc_list(size_t sz, struct tmpl_list_el ***ls)
{
	int i;

	/* alloc array */
	if ((*ls = (struct tmpl_list_el **)calloc(sz + 1, \
			sizeof(struct tmpl_list_el *))) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	for (i = 0; i < sz; i++) {
		if (((*ls)[i] = (struct tmpl_list_el *)malloc( \
				sizeof(struct tmpl_list_el))) == NULL) {
			vztt_logger(0, errno, "Cannot alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
		memset((void *)(*ls)[i], 0, sizeof(struct tmpl_list_el));
		if (((*ls)[i]->info = (struct tmpl_info *)malloc( \
				sizeof(struct tmpl_info))) == NULL) {
			vztt_logger(0, errno, "Cannot alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
		memset((void *)(*ls)[i]->info, 0, sizeof(struct tmpl_info));
	}
	(*ls)[i] = NULL;

	return 0;
}

/* get template list */
int tmplset_get_list(
		struct tmpl_set *t,
		struct global_config *gc,
		struct options_vztt *opts_vztt,
		struct tmpl_list_el ***ls)
{
	int rc;
	struct os_tmpl_list_el *o;
	struct app_tmpl_list_el *a;
	size_t sz, i;

	/* calculate result size */
	sz = 0;
	if (opts_vztt->templates != OPT_TMPL_APP)
		sz += 1 + os_tmpl_list_size(&t->oses);
	if (opts_vztt->templates != OPT_TMPL_OS)
		sz += app_tmpl_list_size(&t->avail_apps);

	/* alloc & init array */
	if ((rc = tmplset_alloc_list(sz, ls)))
		return rc;

	i = 0;
	if (opts_vztt->templates != OPT_TMPL_APP) {
		if ((rc = os_tmpl_get_list_el(gc, (struct os_tmpl *)t->base,
			t->base, t->base->tmpldir, opts_vztt->fld_mask, 0,
			(*ls)[i])))
			return rc;
		i++;

		for (o = t->oses.tqh_first; o != NULL; o = o->e.tqe_next, i++) {
			if ((rc = os_tmpl_get_list_el(gc, o->tmpl, t->base, \
				t->base->tmpldir, opts_vztt->fld_mask, 0,
				(*ls)[i])))
				return rc;
		}
	}

	if (opts_vztt->templates != OPT_TMPL_OS) {
		for (a = t->avail_apps.tqh_first; a != NULL;
						a = a->e.tqe_next, i++){
			if ((rc = app_tmpl_get_list_el(a->tmpl, \
				t->base->tmpldir, opts_vztt->fld_mask, 0,
				(*ls)[i])))
				return rc;
			/* import arch from base os template */
			if (((*ls)[i]->info->osarch =
					strdup(t->base->osarch)) == NULL) {
				vztt_logger(0, errno, "Cannot alloc memory");
				return VZT_CANT_ALLOC_MEM;
			}
		}
	}
	(*ls)[i] = NULL;

	return 0;
}

/* get VE template list */
int tmplset_get_ve_list(
		struct tmpl_set *t,
		struct global_config *gc,
		char *ve_private,
		struct options_vztt *opts_vztt,
		struct tmpl_list_el ***ls)
{
	int rc;
	struct app_tmpl_list_el *a;
	size_t sz, i;

	/* calculate result size */
	sz = 0;
	if (opts_vztt->templates != OPT_TMPL_APP)
		sz += 1;
	if (opts_vztt->templates != OPT_TMPL_OS)
		sz += app_tmpl_list_size(&t->used_apps);

	/* alloc & init array */
	if ((rc = tmplset_alloc_list(sz, ls)))
		return rc;

	i = 0;
	if (opts_vztt->templates != OPT_TMPL_APP) {
		if (t->os->base == NULL) {
			if ((rc = os_tmpl_get_list_el(gc, t->os,
				(struct base_os_tmpl *)t->os,
				ve_private, opts_vztt->fld_mask, 1, (*ls)[i])))
				return rc;
		} else {
			if ((rc = os_tmpl_get_list_el(gc, t->os,
				(struct base_os_tmpl *)t->os->base,
				ve_private, opts_vztt->fld_mask, 1, (*ls)[i])))
				return rc;
		}
		(*ls)[i++]->is_os = 1;
	}

	if (opts_vztt->templates != OPT_TMPL_OS) {
		for (a = t->used_apps.tqh_first; a != NULL; a = a->e.tqe_next){
			if ((rc = app_tmpl_get_list_el(a->tmpl, \
					ve_private, opts_vztt->fld_mask,
					!(opts_vztt->flags & OPT_VZTT_AVAILABLE),
					(*ls)[i++])))
				return rc;
		}
	}
	(*ls)[i] = NULL;

	return 0;
}

/* 0 - ve OSTEMPLATE is equal <data> */
int os_selector(const char *ctid, void *data)
{
	int rc = 1;
	char *ostemplate;
	int tmpl_type;

	if (ve_config_ostemplate_read(ctid, &ostemplate, &tmpl_type))
		return rc;
	if (ostemplate == NULL)
		return rc;
	if (tmpl_type == VZ_TMPL_EZ)
		rc = strcmp((char *)data, ostemplate);

	free((void *)ostemplate);
	return rc;
}

/* get list of ve's, for which OSTEMPLATE is <t->os> */
int tmplset_get_velist_for_os(
		struct tmpl_set *t,
		struct string_list *ls)
{
	int rc;

	if ((rc = get_ve_list(ls, os_selector, (void *)t->os->name)))
		return rc;

	return 0;
}

/* 0 - ve OSTEMPLATE is in <data> string list */
int base_os_selector(const char *ctid, void *data)
{
	int rc = 1;
	char *ostemplate = NULL;
	struct string_list *os_list = (struct string_list *)data;
	struct string_list_el *s;
	int tmpl_type;

	if (ve_config_ostemplate_read(ctid, &ostemplate, &tmpl_type))
		return rc;
	if (ostemplate == NULL)
		return rc;
	if (tmpl_type == VZ_TMPL_EZ) {
		string_list_for_each(os_list, s) {
			if ((rc = strcmp(ostemplate, s->s)) == 0)
				break;
		}
	}
	free((void *)ostemplate);
	return rc;
}

/* get list of ve's, for which <OSTEMPLATE> is <t->base>
or <OSTEMPLATE> is in <t->oses> list */
int tmplset_get_velist_for_base(
		struct tmpl_set *t,
		struct string_list *ls)
{
	int rc;
	struct string_list os_list;
	struct os_tmpl_list_el *o;

	string_list_init(&os_list);
	string_list_add(&os_list, t->base->name);
	for (o = t->oses.tqh_first; o != NULL; o = o->e.tqe_next)
		string_list_add(&os_list, o->tmpl->name);

	if ((rc = get_ve_list(ls, base_os_selector, (void *)&os_list)))
		return rc;
	string_list_clean(&os_list);

	return 0;
}

