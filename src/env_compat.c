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
 * Our contact details: Parallels IP Holdings GmbH, Vordergasse 59, 8200
 * Schaffhausen, Switzerland.
 *
 * Commom rpm functions for yum and zypper
 */

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <dirent.h>

#include "downloader.h"
#include "env_compat.h"
#include "queue.h"
#include "tmplset.h"
#include "transaction.h"
#include "util.h"
#include "vztt_error.h"
#include "progress_messages.h"

/* Read rpm package(s) info from <fp>, parse and
   put into struct pkg_info * list <ls> */

int read_rpm_info(FILE *fp, void *data)
{
	char buf[PATH_MAX+1];
	char *str;
	int is_descr = 0;
	struct pkg_info *p = NULL;
	struct string_list description;
	struct pkg_info_list *ls = (struct pkg_info_list *)data;

	string_list_init(&description);
	while(fgets(buf, sizeof(buf), fp)) {
		if (strncmp(buf, NAME_TITLE_RPM, strlen(NAME_TITLE_RPM)) == 0) {
			is_descr = 0;
			str = cut_off_string(buf + strlen(NAME_TITLE_RPM));
			if (str == NULL)
				continue;
			p = (struct pkg_info *)malloc(sizeof(struct pkg_info));
			if (p == NULL) {
				vztt_logger(0, errno, "Cannot alloc memory");
				return VZT_CANT_ALLOC_MEM;
			}
			p->name = strdup(str);
			p->version = NULL;
			p->release = NULL;
			p->arch = NULL;
			p->summary = NULL;
			p->description = NULL;
			string_list_init(&description);
			pkg_info_list_add(ls, p);
			continue;
		}
		if (p == NULL)
			continue;

		if (strncmp(buf, ARCH_TITLE_RPM, \
				strlen(ARCH_TITLE_RPM)) == 0) {
			str = cut_off_string(buf + strlen(ARCH_TITLE_RPM));
			if (str)
				p->arch = strdup(str);
		}
		else if (strncmp(buf, VERSION_TITLE, \
				strlen(VERSION_TITLE)) == 0) {
			str = cut_off_string(buf + strlen(VERSION_TITLE));
			if (str)
				p->version = strdup(str);
		}
		else if (strncmp(buf, RELEASE_TITLE, \
				strlen(RELEASE_TITLE)) == 0) {
			str = cut_off_string(buf + strlen(RELEASE_TITLE));
			if (str)
				p->release = strdup(str);
		}
		else if (strncmp(buf, SUMMARY_TITLE, \
				strlen(SUMMARY_TITLE)) == 0) {
			str = cut_off_string(buf + strlen(SUMMARY_TITLE));
			if (str)
				p->summary = strdup(str);
		}
		else if (strncmp(buf, DESC_TITLE, \
				strlen(DESC_TITLE)) == 0) {
			str = cut_off_string(buf + strlen(DESC_TITLE));
			if (str)
				string_list_add(&description, str);
			is_descr = 1;
		} else if (is_descr) {
			if ((str = cut_off_string(buf)) == NULL) {
				if (p->description == NULL)
					string_list_to_array(&description, \
						&p->description);
				is_descr = 0;
			} else
				string_list_add(&description, str);
		}
	}
	if (p)
		if (p->description == NULL)
			string_list_to_array(&description, &p->description);
	string_list_clean(&description);

	return 0;
}

/* try ot download mirrorlist to local temparary file */
int fetch_mirrorlist(
		struct Transaction *pm,
		char *mirrorlist,
		char *buf,
		int size)
{
	char *net_protos[] = {"http:", "https:", "ftp:"};
	int islocal = 1;
	size_t i;
	int md;
	int rc = 0;
	struct _url *proxy = NULL;

	/* is mirrorlist in network ? */
	for (i = 0; i < sizeof(net_protos)/sizeof(char *); i++) {
		if (strncmp(mirrorlist, net_protos[i], \
			strlen(net_protos[i])) == 0) {
			islocal = 0;
			break;
		}
	}
	if (islocal) {
		/* copy first location */
		strncpy(buf, mirrorlist, size);
		return 0;
	}

	/* download mirrorlist */
	/* create temporary file */
	snprintf(buf, size, "%s/mirrorlist.XXXXXX", pm->tmpdir);
	if ((md = mkstemp(buf)) == -1) {
		vztt_logger(0, errno, "mkstemp(%s) error", buf);
		return VZT_CANT_CREATE;
	}
	rc = write(md, " ", 1);
	close(md);

	if (rc == -1)
		return vztt_error(VZT_SYSTEM, errno, "write()");

	/* fetch file */
	if (pm->http_proxy.server)
		proxy = &pm->http_proxy;
	else if (pm->ftp_proxy.server)
		proxy = &pm->ftp_proxy;
	else if (pm->https_proxy.server)
		proxy = &pm->https_proxy;
	if ((rc = fetch_file(pm->tmpdir, mirrorlist, proxy, buf, pm->debug)))
		unlink(buf);

	return rc;
}

/* Read rpm package(s) data from <fp>, parse and
   put into struct package * list <ls> */
static int read_rpm(FILE *fp, void *data)
{
	char buf[PATH_MAX+1];
	struct package *pkg;
	struct package_list *packages = (struct package_list *)data;
	int rc;

	while(fgets(buf, sizeof(buf), fp)) {
		if ((rc = parse_p(buf, &pkg)))
			return rc;

		package_list_insert(packages, pkg);
	}

	return 0;
}

/*
 get installed into VE rpms list
 use external pm to work on stopped VE
 package manager root pass as extern parameter
 to use root (for runned) and private (for stopped VEs) areas.
*/
int env_compat_get_install_pkg(struct Transaction *pm, struct package_list *packages)
{
	int rc = 0;
	struct string_list args;
	struct string_list envs;
	struct package_list_el *p;

	string_list_init(&args);
	string_list_init(&envs);

	/* rpm parameters */
	string_list_add(&args, "-qa");
	string_list_add(&args, "--root");
	string_list_add(&args, pm->rootdir);
	/* attn: only _one_ space as delimiter,
	string can not start from space */
	string_list_add(&args, "--qf");
	string_list_add(&args, "\%\{NAME} "\
		"\%|EPOCH\?{\%\{EPOCH}:}|\%\{VERSION}-\%\{RELEASE} \%\{ARCH} "\
		"\%\{SUMMARY}\\n");

	/* add templates environments too */
	if ((rc = add_tmpl_envs(pm->tdata, &envs)))
		return rc;

	/* Enable checker */
	create_veroot_unjump_checker(pm, &envs);

	/* run cmd from chroot environment */
	rc = run_from_chroot2("/usr/lib/rpm/rpmq", pm->envdir, pm->debug,
		pm->ign_pm_err, &args, &envs, pm->osrelease, read_rpm,
		(void *)packages);

	/* free mem */
	string_list_clean(&args);
	string_list_clean(&envs);

	if (pm->debug >= 4) {
		vztt_logger(4, 0, "Installed packages are:");
		/* copy to out list */
		for (p = packages->tqh_first; p != NULL; p = p->e.tqe_next)
			vztt_logger(4, 0, "\t%s %s %s", \
				p->p->name, p->p->evr, p->p->arch);
	}
	return rc;
}

/* remove rpm environment files and create lock dir */
int env_compat_fix_pkg_db(struct Transaction *pm)
{
	char buf[PATH_MAX+1];

	/* try to fix rpm database */
	if (pm->rootdir == NULL)
		return 0;

	snprintf(buf, sizeof(buf), "%s/var/lib/rpm/__db.001", pm->rootdir);
	if (access(buf, F_OK) == 0)
		unlink(buf);
	snprintf(buf, sizeof(buf), "%s/var/lib/rpm/__db.002", pm->rootdir);
	if (access(buf, F_OK) == 0)
		unlink(buf);
	snprintf(buf, sizeof(buf), "%s/var/lib/rpm/__db.003", pm->rootdir);
	if (access(buf, F_OK) == 0)
		unlink(buf);

	/* create transaction lock directory */
	snprintf(buf, sizeof(buf), "%s/var/lock/rpm", pm->rootdir);
	if (access(buf, F_OK) != 0)
		mkdir(buf, 0777);

	return 0;
}

/* get human package name */
static void get_pkgname(struct package *pkg, char *name, int size)
{
	/* yes, rpm without arch already exist:
[root@zeus vztt]# vzctl exec 3026 rpm -q gpg-pubkey-4f2a6fd2-3f9d9d3b --qf "${ARCH}"
[root@zeus vztt]# vzpkg list 3026
fedora-core-4-x86                       2006-08-29 12:43:37
*/
	if (pkg->arch && strlen(pkg->arch))
		snprintf(name, size, "%s-%s.%s", pkg->name, pkg->evr, pkg->arch);
	else
		snprintf(name, size, "%s-%s", pkg->name, pkg->evr);
}

/* get rpm package name
   Specifying rpm package names:
	name
	name.arch
	name-version
	name-version-release
	name-version-release.arch
	epoch:name-version-release.arch
*/
int env_compat_get_int_pkgname(struct package *pkg, char *name, int size)
{
	char *buf, *pe, *pv;

	if ((buf = strdup(pkg->evr)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	/* parse p->evr */
	strcpy(buf, pkg->evr);
	if ((pv = strchr(buf, ':'))) {
		pe = buf;
		*(pv++) = '\0';
	}
	else {
		pe = NULL;
		pv = buf;
	}
	name[0] = '\0';
	if (pe) {
		strncat(name, pe, size-strlen(name)-1);
		strncat(name, ":", size-strlen(name)-1);
	}
	strncat(name, pkg->name, size-strlen(name)-1);
	strncat(name, "-", size-strlen(name)-1);
	strncat(name, pv, size-strlen(name)-1);
	if (pkg->arch && strlen(pkg->arch)) {
		strncat(name, ".", size-strlen(name)-1);
		strncat(name, pkg->arch, size-strlen(name)-1);
	}

	return 0;
}

/* get name without version */
void env_compat_get_short_pkgname(struct package *pkg, char *name, int size)
{
	if (pkg->arch && strlen(pkg->arch))
		snprintf(name, size, "%s.%s", pkg->name, pkg->arch);
	else
		snprintf(name, size, "%s", pkg->name);
}

/* check that pkgdir is package directory for standard template */
int env_compat_is_std_pkg_area(const char *pkgdir, struct package *pkg)
{
	char dir[NAME_MAX+1];
	char *vr;

	/* 2 variants : without epoch and arch and with them */
	/* check full variant */
	get_pkgname(pkg, dir, sizeof(dir));
	if (strcmp(pkgdir, dir) == 0)
		return 1;

	if ((vr = strchr(pkg->evr, ':')) == NULL)
		vr = pkg->evr;
	else
		vr++;
	/* check short variant */
	snprintf(dir, sizeof(dir), "%s-%s", pkg->name, vr);
	if (strcmp(pkgdir, dir) == 0)
		return 1;

	return 0;
}

/* create root for rpm at <dir> */
int env_compat_create_root(char *dir)
{
	char path[PATH_MAX];

        /* create mandatory dirs */
	snprintf(path, sizeof(path), "%s/var", dir);
	if (access(path, F_OK))
		if (mkdir(path, 0755))
		{
			vztt_logger(0, errno, "Can not create %s directory", \
				path);
			return VZT_CANT_CREATE;
		}
	snprintf(path, sizeof(path), "%s/var/lib", dir);
	if (access(path, F_OK))
		if (mkdir(path, 0755))
		{
			vztt_logger(0, errno, "Can not create %s directory", \
				path);
			return VZT_CANT_CREATE;
		}
	snprintf(path, sizeof(path), "%s/var/lib/rpm", dir);
	if (access(path, F_OK))
		if (mkdir(path, 0755))
		{
			vztt_logger(0, errno, "Can not create %s directory", \
				path);
			return VZT_CANT_CREATE;
		}

	return 0;
}

/* Find package directory in EZ template area
*/
static int find_pkg(
		struct Transaction *pm,
		struct package *pkg,
		char *subdir,
		char *dir,
		size_t size)
{
	char *p = pkg->evr;
	int rc = 0;
	char path[PATH_MAX+1];

	get_pkgname(pkg, dir, size);
	snprintf(path, sizeof(path), "%s/%s/%s", \
		pm->tmpldir, subdir, dir);
	if (access(path, R_OK) == 0)
		return 1;

	if (pkg->evr[0] == '0' && pkg->evr[1] == ':') {
		/* if epoch = 0, try to seek directory without epoch at all */
		pkg->evr += 2;
		get_pkgname(pkg, dir, size);
		snprintf(path, sizeof(path), "%s/%s/%s", \
			pm->tmpldir, subdir, dir);
		if (access(path, R_OK) == 0)
			rc = 1;
		else
			rc = 0;
		pkg->evr = p;
	} else {
		/* backward - try to seek directrory with epoch */
		char path[PATH_MAX+1];

		if (pkg->arch && strlen(pkg->arch))
			snprintf(dir, size, "%s-0:%s.%s", \
				pkg->name, pkg->evr, pkg->arch);
		else
			snprintf(dir, size, "%s-0:%s", \
				pkg->name, pkg->evr);
		snprintf(path, sizeof(path), "%s/%s/%s", \
			pm->tmpldir, subdir, dir);
		if (access(path, R_OK) == 0)
			return 1;
	}

	return rc;
}

/* Find package directory in EZ template area
   Parameters:
   - tmpldir :	template directory (/vz/template)
   - basesubdir : OS template subdirectory (debian/3.1/x86)
   - pkg :	package structure
   Attn: pkg->evr may has 0 epoch instead of (none)
   and backward: pkg->evr may has not epoch, but package directory - has.
*/
int env_compat_find_pkg_area(
		struct Transaction *pm,
		struct package *pkg)
{
	char dir[PATH_MAX];

	return find_pkg(pm, pkg, pm->basesubdir, dir, sizeof(dir));
}

/* find package directory in template */
int env_compat_find_pkg_area2(
		struct Transaction *pm,
		struct package *pkg)
{
	char dir[PATH_MAX];

	/* 1. seek in template area */
	if (find_pkg(pm, pkg, pm->basesubdir, dir, sizeof(dir)))
		return 1;
	return 0;
}

/* Find package directory in EZ template area
*/
int env_compat_find_pkg_area_ex(
		struct Transaction *pm,
		struct package *pkg,
		char *dir,
		size_t size)
{
	return find_pkg(pm, pkg, pm->basesubdir, dir, size);
}

static int ver_cmp(const char *str1, const char *str2)
{
	const char *p1, *p2;
	int ret;

	p1 = str1;
	p2 = str2;
	while (1) {
		/* skip till the next valuable block */
		while (*p1 && (!isalnum(*p1) || *p1 == '0'))
			p1++;
		while (*p2 && (!isalnum(*p2) || *p2 == '0'))
			p2++;
		if (!*p1 || !*p2)
			break;

		str1 = p1;
		str2 = p2;
		if (isdigit(*p1)) {
			/* compare numeric */
			if (!isdigit(*p2))
				return -1;
			while (*p1 && isdigit(*p1))
				p1++;
			while (*p2 && isdigit(*p2))
				p2++;
			if ((p1 - str1) > (p2 - str2))
				return 1;
			else if ((p1 - str1) < (p2 - str2))
				return -1;
			else {
				ret = strncmp(str1, str2, p1 - str1);
				if (ret)
					return ret;
			}
		} else {
			/* compare strings */
			if (!isalpha(*p2))
				return -1;
			while (*p1 && isalpha(*p1) && *p2 && isalpha(*p2)) {
				if (*p1 > *p2)
					return 1;
				else if (*p2 > *p1)
					return -1;
				p1++;
				p2++;
			}
			if ((!*p1 || !isalpha(*p1)) && *p2 && isalpha(*p2))
				return -1;
			else if (*p1 && isalpha(*p1) && (!*p2 || !isalpha(*p2)))
				return 1;
		}
	}
	/* the only way to escape from cycle is one string's end */
	if (!*p2 && *p1)
		return 1;
	else if (!*p1 && *p2)
		return -1;
	return 0;
}

/* compare two versions:
 *eval = 1: a is newer than b
         0: a and b are the same version
        -1: b is newer than a */
int env_compat_ver_cmp(struct Transaction *pm, const char * a, const char * b, int *eval)
{
	*eval = ver_cmp(a,b);
	return 0;
}

/* update metadata */
int env_compat_update_metadata(struct Transaction *pm, const char *name)
{
	int rc;
	char path[PATH_MAX];
	char progress_stage[PATH_MAX];
	char *of = pm->outfile;
	int td;
	int data_source = pm->data_source;

	snprintf(progress_stage, sizeof(progress_stage),
		PROGRESS_PROCESS_METADATA, name);
	progress(progress_stage, 0, pm->progress_fd);

	/* update package database */
	/* it is not needs to reset rootdir - metadata
	will save in template area in any case */
	if ((rc = pm->pm_action(pm, VZPKG_CLEAN_METADATA, NULL)))
		return rc;

	/* update metadata (primary and filelists) */
	if ((rc = pm->pm_action(pm, VZPKG_MAKECACHE, NULL)))
		return rc;

	/* create temporary outfile */
	snprintf(path, sizeof(path), "%s/outfile.XXXXXX", pm->tmpdir);
	if ((td = mkstemp(path)) == -1) {
		vztt_logger(0, errno, "mkstemp(%s) error", path);
		return VZT_CANT_CREATE;
	}
	close(td);
	if ((pm->outfile = strdup(path)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory\n");
		return VZT_CANT_ALLOC_MEM;
	}

	/* get full packages list */
	pm->data_source = OPT_DATASOURCE_REMOTE;
	rc = pm->pm_action(pm, VZPKG_LIST, NULL);
	pm->data_source = data_source;
	if (rc)
		return rc;

	/* and save metadata */
	if ((rc = pm_save_metadata(pm, name)))
		return rc;

	progress(progress_stage, 100, pm->progress_fd);

	free((void *)pm->outfile);
	pm->outfile = of;
	return 0;
}

/*
   compare conventional package name from template %packages section
   and full name from package struct
   Specifying rpm package names:
	name
	name.arch
	name-version
	name-version-release
	name-version-release.arch
	epoch:name-version-release.arch
   0 - if success
*/
int env_compat_pkg_cmp(const char *pkg, struct package *p)
{
	char *ptr = (char *)pkg, *s;
	char *pe, *pv, *pr;
	char *buf;

	/* 1 - name only */
	if (strcmp(ptr, p->name) == 0)
		return 0;

	/* parse p->evr */
	if ((buf = strdup(p->evr)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	if ((pv = strchr(buf, ':'))) {
		pe = buf;
		*(pv++) = '\0';
	} else {
		pe = NULL;
		pv = buf;
	}
	if ((pr = strchr(pv, '-')))
		*(pr++) = '\0';

	/* 2 - check epoch */
	if ((s = strchr(ptr, ':'))) {
		/* check epoch: ignore epoch 0 */
		if (strncmp(ptr, "0:", 2)) {
			if (pe == NULL)
				return 1;
			if (strncmp(ptr, pe, strlen(pe)))
				return 1;
		}
		ptr = s + 1;
	}

	/* 3 - check name-* */
	if (strncmp(ptr, p->name, strlen(p->name)))
		return 1;
	ptr += strlen(p->name);
	if (*ptr != '-' && *ptr != '.')
		return 1;

	if (*ptr == '-') {
		if (strncmp(++ptr, pv, strlen(pv)))
			return 1;
		ptr += strlen(pv);
		if (*ptr == '-') {
			if (pr == NULL)
				return 1;
			if (strncmp(++ptr, pr, strlen(pr)))
				return 1;
			ptr += strlen(pr);
		}
	}

	if (*ptr == '.') {
		if (strcmp(++ptr, p->arch))
			return 1;
		ptr += strlen(p->arch);
	}

	if (*ptr == '\0')
		return 0;

	return 1;
}

/* Return 1 only for vzpkgenv41s9. All other rpms are newer that 4.1* */
int get_rpm_ver(struct Transaction *pm)
{
	if (strstr(pm->envdir, "rpm41s9"))
		return 1;
	else
		return 2;
}

/*
 remove packages from VE by rpmi from environment
*/
static int remove_pkg_byrpm(struct Transaction *pm,
		struct string_list *packages,
		int allmatches)
{
	char buf[100];
	int rc = 0;
	struct string_list args;
	struct string_list envs;
	struct string_list_el *p;

	if (string_list_empty(packages))
		return VZT_BAD_PARAM;

	string_list_init(&args);
	string_list_init(&envs);

	/* rpm parameters */
	string_list_add(&args, "-e");
	string_list_add(&args, "--root");
	string_list_add(&args, pm->rootdir);
	string_list_add(&args, "--veid");
	snprintf(buf, sizeof(buf), "%s", pm->ctid);
	string_list_add(&args, buf);
	if (pm->force)
		string_list_add(&args, "--nodeps");
	if (pm->test)
		string_list_add(&args, "--test");
	if (allmatches)
		string_list_add(&args, "--allmatches");

	/* add package names to command */
	for (p = packages->tqh_first; p != NULL; p = p->e.tqe_next)
		string_list_add(&args, p->s);

	/* Enable checker */
	create_veroot_unjump_checker(pm, &envs);

	/* add templates environments too */
	if ((rc = add_tmpl_envs(pm->tdata, &envs)))
		return rc;

	/* run cmd from chroot environment */
	rc = run_from_chroot("/usr/lib/rpm/rpmi", pm->envdir, pm->debug,
			pm->ign_pm_err, &args, &envs, pm->osrelease);

	/* free mem */
	string_list_clean(&args);
	string_list_clean(&envs);

	return rc;
}

/* remove packages */
int env_compat_remove_rpm(
		struct Transaction *pm,
		struct string_list *packages,
		struct package_list *remains,
		struct package_list *removed)
{
	int rc = 0;
	char buf[PATH_MAX];
	int lfound = 0, mix_templates = 0;

	struct package_list_el *p;
	struct string_list_el *i;
	struct string_list args;
	struct package_list empty;

	string_list_init(&args);

	/*
	 eto zasada - from yum page:
	 remove or erase
              Are  used to remove the specified packages from the system
	      as well as removing any packages which depend on the
              package being removed.
	*/
	if (pm->depends)
		return pm_modify(pm, VZPKG_REMOVE, packages, &empty, removed);

	/* to remove selected packages only by rpm */
	if (get_rpm_ver(pm) > 1) {
		char *evr;
		/* it's smart rpm,
		it understand architectures in package names
		therefore do not use --allmatches option and
		list of removing is equal list of removed.
		now find removing packages in installed */
		for (i = packages->tqh_first; i != NULL; i = i->e.tqe_next) {
			lfound = 0;
			mix_templates = 0;
			/* it's may be some matches for one args */
			for (p = remains->tqh_first; p != NULL; \
					p = p->e.tqe_next) {
				/* Check that package present in other template
				   if so - skip it */
				if (env_compat_pkg_cmp(i->s, p->p))
				{
					mix_templates = 1;
					continue;
				}
				/* Check that package already removed
				   if so - skip it too */
				if (package_list_find(removed, p->p))
					continue;
				/* add into arguments exact match instead of
				initial arg, because of rpm will not
				use --allmatches option */
				/* skip epoch in p->evr */
				if ((evr = strchr(p->p->evr, ':')))
					evr++;
				else
					evr = p->p->evr;
				snprintf(buf, sizeof(buf), "%s-%s", \
					p->p->name, evr);
				if (p->p->arch && strlen(p->p->arch)) {
					strncat(buf, ".", \
						sizeof(buf)-strlen(buf)-1);
					strncat(buf, p->p->arch, \
						sizeof(buf)-strlen(buf)-1);
				}
				lfound = 1;
				string_list_add(&args, buf);
				package_list_add(removed, p->p);
			}
			if (!lfound && !mix_templates) {
				vztt_logger(0, 0, "Package %s is not installed", i->s);
				rc = VZT_BAD_PARAM;
				goto cleanup_1;
			}
		}
		if (string_list_size(&args))
			rc = remove_pkg_byrpm(pm, &args, 0);
	} else {
		char *ptr;
		for (i = packages->tqh_first; i != NULL; i = i->e.tqe_next) {
			lfound = 0;
			mix_templates = 0;
			/* remove arch from args */
			strncpy(buf, i->s, sizeof(buf));
			if ((ptr = strchr(buf, '.')))
				*ptr = '\0';
			/* it's may be some matches for one args */
			for (p = remains->tqh_first; p != NULL; \
					p = p->e.tqe_next) {
				/* Check that package present in other template
				   if so - skip it */
				if (env_compat_pkg_cmp(buf, p->p))
				{
					mix_templates = 1;
					continue;
				}
				/* Check that package already removed
				   if so - skip it too */
				if (package_list_find(removed, p->p))
					continue;
				package_list_add(removed, p->p);
				lfound = 1;
			}
			if (lfound) {
				string_list_add(&args, buf);
			} else if (mix_templates) {
				continue;
			} else {
				vztt_logger(0, 0, "Package %s is not installed", i->s);
				rc = VZT_BAD_PARAM;
				goto cleanup_1;
			}
		}
		if (string_list_size(&args))
			rc = remove_pkg_byrpm(pm, &args, 1);
	}

cleanup_1:
	string_list_clean(&args);
	return rc;
}

/* create OS template cache - install OS template packages */
int env_compat_create_cache(
		struct Transaction *pm,
		struct string_list *packages0,
		struct string_list *packages1,
		struct string_list *packages,
		struct package_list *installed)
{
	int rc = 0;
	struct package_list empty;

	package_list_init(&empty);
	if (!string_list_empty(packages0)) {
		if ((rc = pm_modify(pm, VZPKG_INSTALL,
				packages0, installed, &empty)))
			goto cleanup;
	}
	if (!string_list_empty(packages1)) {
		if ((rc = pm_modify(pm, VZPKG_INSTALL,
				packages1, installed, &empty)))
			goto cleanup;
	}
	if (!string_list_empty(packages)) {
		rc = pm_modify(pm, VZPKG_INSTALL, packages, installed, &empty);
	}

cleanup:
	package_list_clean(&empty);
	return  rc;
}

/* clean yum local cache and remove all objects from <basedir>/pm/ */
int env_compat_clean_local_cache(struct Transaction *pm)
{
	int rc = 0;
	char dir[PATH_MAX+1];
	char path[PATH_MAX+1];
	DIR * d;
	char dirent_buf[sizeof(struct dirent) + PATH_MAX + 1];
	struct dirent *de = (struct dirent *) dirent_buf;
	struct dirent *result;
	int retval;
	struct stat st;

	snprintf(dir, sizeof(dir), "%s/%s", pm->basedir, pm->datadir);
	if (access(dir, F_OK))
		return 0;

	if ((rc = pm->pm_action(pm, VZPKG_CLEAN, NULL)))
		return rc;

	/* clean and remove all subdirs in <basedir>/pm/ */
	if ((d = opendir(dir)) == NULL) {
		vztt_logger(0, errno, "opendir(\"%s\") error", dir);
		return VZT_CANT_OPEN;
	}

	while (1) {
		if ((retval = readdir_r(d, de, &result)) != 0)
		{
			vztt_logger(0, retval, "readdir_r(\"%s\") error",
					dir);
			rc = VZT_CANT_READ;
			break;
		}

		if (result == NULL)
			break;

		if(strcmp(de->d_name, ".") == 0)
			continue;

		if(strcmp(de->d_name, "..") == 0)
			continue;

		snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
		if (lstat(path, &st))
			continue;

		if (S_ISDIR(st.st_mode))
			remove_directory(path);
		else
			unlink(path);
	}
	closedir(d);

	return rc;
}

/*
  get package info from rpm, parse and place in structures
*/
int env_compat_rpm_get_info(
		struct Transaction *pm,
		const char *package,
		struct pkg_info_list *ls)
{
	int rc = 0;
	struct string_list args;
	struct string_list envs;

	string_list_init(&args);
	string_list_init(&envs);

	/* rpm parameters */
	string_list_add(&args, "-q");
	string_list_add(&args, (char *)package);
	string_list_add(&args, "--root");
	string_list_add(&args, pm->rootdir);
	string_list_add(&args, "--qf");
	string_list_add(&args, NAME_TITLE_RPM "\%\{NAME}\\n"\
		ARCH_TITLE_RPM "\%\{ARCH}\\n"\
		VERSION_TITLE "\%|EPOCH\?{\%\{EPOCH}:}|\%\{VERSION}\\n"\
		RELEASE_TITLE "\%\{RELEASE}\\n"\
		SUMMARY_TITLE "\%\{SUMMARY}\\n"\
		DESC_TITLE "\%\{DESCRIPTION}\\n\\n");

	/* Enable checker */
	create_veroot_unjump_checker(pm, &envs);

	/* add templates environments too */
	if ((rc = add_tmpl_envs(pm->tdata, &envs)))
		return rc;

	/* run cmd from chroot environment */
	rc = run_from_chroot2("/usr/lib/rpm/rpmq", pm->envdir, pm->debug,
		pm->ign_pm_err, &args, &envs, pm->osrelease,
		read_rpm_info, (void *)ls);

	/* free mem */
	string_list_clean(&args);
	string_list_clean(&envs);

	return rc;
}

/* migrate url list to corresponding vztt proxy url list */
static int url2vzttproxy(
		struct Transaction *pm,
		struct string_list *urls,
		int *num,
		struct repo_list *repos)
{
	int rc;
	size_t size;
	struct _url u;
	struct string_list_el *p;
	char *buf;

	string_list_for_each(urls, p) {
		if (parse_url(p->s, &u))
			continue;
		/* create mirror url */
		size = strlen(pm->vzttproxy) + 1 +
			strlen(u.server) + 1 + strlen(u.path) + 1;
		if ((buf = (char *)malloc(size)) == NULL)
			return vztt_error(VZT_SYSTEM, errno, "malloc()");
		snprintf(buf, size, "%s/%s/%s",
			pm->vzttproxy, u.server, u.path);
		if ((rc = repo_list_add(repos, buf, "vzttproxy", (*num)++)))
			return rc;
		free((void *)buf);
		/* TODO: free_url(&u); */
	}
	return 0;
}

/*
  Parse repository or mirrorlist record: part to separate urls
  And replace substring url_map[i]->src by url_map[i]->dst in src and place to *dst.
  Only one replacement is possible.
*/
int env_compat_parse_repo_rec(
		const char *rec,
		struct url_map_list *url_map,
		struct string_list *ls,
		int force)
{
	int rc = 0;
	char *buf, *str, *token, *delim=" 	";
	char url[2*PATH_MAX];
	char *yum_vars[] = {"$releasever", "$arch", "$basearch",
		"$YUM0", "$YUM1", "$YUM2", "$YUM3", "$YUM4", "$YUM5",
		"$YUM6", "$YUM7", "$YUM8", "$YUM9", "$YUM10", NULL };
	char *saveptr;

	/* strtok_r damage string - use buffer */
	if ((buf = strdup(rec)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	for (str = buf; ;str = NULL) {
		token = strtok_r(str, delim, &saveptr);
		if (token == NULL)
			break;

		/* use replacement in url_map (host1.domain1 -> host2.domain2) */
		prepare_url(url_map, token, url, sizeof(url));

		/* check non-yum $* variable in url */
		if ((rc = pm_check_url(url, yum_vars, force)))
			break;

		if ((rc = string_list_add(ls, url)))
			break;
	}

	free((void *)buf);
	return rc;
}

/*
 Use vzttproxy for repair:
 Replace all repositories on corresponding vzttproxy url.
 Alternatile urls set (and mirrorlists too) convert to set
 of separate vzttproxy urls.
*/
int env_compat_vzttproxy_fetch(
		struct Transaction *pm,
		struct package *pkg)
{
	int rc = 0;
	int lfound;
	char path[PATH_MAX+1];
	struct repo_rec *r;
	struct repo_rec *z;
	struct string_list urls;
	struct string_list_el *p;
	struct string_list args;
	char *vzttproxy;
	int num = 0;
	struct repo_list repos;
	struct repo_list zypp_repos;

	if (pm->vzttproxy == NULL)
		return VZT_INTERNAL;

	repo_list_init(&repos);
	repo_list_init(&zypp_repos);
	string_list_init(&args);
	string_list_init(&urls);

	/* replace repositories to vzttproxy urls
	   alternative urls replace by separated one */
	repo_list_for_each(&pm->repositories, r) {
		string_list_init(&urls);
		if (env_compat_parse_repo_rec(r->url, pm->url_map, &urls, pm->force))
			continue;

		if ((rc = url2vzttproxy(pm, &urls, &num, &repos)))
			return rc;

		string_list_clean(&urls);
	}
	repo_list_for_each(&pm->zypp_repositories, z) {
		string_list_init(&urls);
		if (env_compat_parse_repo_rec(z->url, pm->url_map, &urls, pm->force))
			continue;

		if ((rc = url2vzttproxy(pm, &urls, &num, &zypp_repos)))
			return rc;

		string_list_clean(&urls);
	}

	/* process mirrorlists */
	repo_list_for_each(&pm->mirrorlists, r) {
		string_list_init(&urls);
		if (env_compat_parse_repo_rec(r->url, pm->url_map, &urls, pm->force))
			continue;

		lfound = 0;
		#pragma GCC diagnostic ignored "-Waddress"
		string_list_for_each(&urls, p) {
			/* try to download mirrorlist */
			if (fetch_mirrorlist((struct Transaction *)pm, p->s, path, \
					sizeof(path)) == 0) {
				lfound = 1;
				break;
			}
		}
		string_list_clean(&urls);
		if (!lfound)
			continue;

		/* rewrote fetched mirrorlist file:
		  dublicate all records for vzttproxy */
		if (string_list_read(path, &urls))
			continue;

		if ((rc = url2vzttproxy(pm, &urls, &num, &repos)))
			return rc;

		string_list_clean(&urls);
	}

	/* remove vzttproxy */
	vzttproxy = pm->vzttproxy;
	pm->vzttproxy = NULL;

	/* clean repositories&mirrorlists */
	repo_list_clean(&pm->repositories);
	repo_list_clean(&pm->zypp_repositories);
	repo_list_clean(&pm->mirrorlists);
	/* redirect repositories to repos */
	r = pm->repositories.tqh_first;
	pm->repositories.tqh_first = repos.tqh_first;
	z = pm->zypp_repositories.tqh_first;
	pm->zypp_repositories.tqh_first = zypp_repos.tqh_first;

	if ((rc = env_compat_get_int_pkgname(pkg, path, sizeof(path))))
		goto cleanup;
	if ((rc = string_list_add(&args, path)))
		goto cleanup;
	rc = pm->pm_action(pm, VZPKG_GET, &args);

cleanup:
	pm->repositories.tqh_first = r;
	pm->zypp_repositories.tqh_first = z;
	string_list_clean(&args);
	repo_list_clean(&repos);
	repo_list_clean(&zypp_repos);
	pm->vzttproxy = vzttproxy;

	return rc;
}

/* last repair chance: try fetch package <pkg> from <repair_mirror>
   Load mirrorlist <repair_mirror>/osname/osver/osarch/repair_mirrorlist
 */
int env_compat_last_repair_fetch(
		struct Transaction *pm,
		struct package *pkg,
		const char *repair_mirror)
{
	int rc = 0;
	char *mirror;
	size_t size;
	char buf[NAME_MAX+1];
	struct string_list args;

	string_list_init(&args);

	/* clean repositories&mirrorlists */
	repo_list_clean(&pm->repositories);
	repo_list_clean(&pm->mirrorlists);
	/* create mirror url */
	size = strlen(repair_mirror) + 1 +
		strlen(pm->tdata->base->osname) + 1 +
		strlen(pm->tdata->base->osver) + 1 +
		strlen(pm->tdata->base->osarch) + 1;
	if ((mirror = (char *)malloc(size)) == NULL)
		return vztt_error(VZT_SYSTEM, errno, "malloc()");
	snprintf(mirror, size, "%s/%s/%s/%s",
		repair_mirror, pm->tdata->base->osname,
		pm->tdata->base->osver, pm->tdata->base->osarch);
	if ((rc = repo_list_add(&pm->repositories, mirror, "repair_mirror", 0)))
		goto cleanup;

	if ((rc = env_compat_get_int_pkgname(pkg, buf, sizeof(buf))))
		goto cleanup;
	if ((rc = string_list_add(&args, buf)))
		goto cleanup;
	rc = pm->pm_action(pm, VZPKG_GET, &args);
	string_list_clean(&args);

cleanup:
	free(mirror);
	return rc;
}

/* parse template area directory name (name-[epoch:]version-release.arch)
and create struct package */
int env_compat_parse_vzdir_name(char *dirname, struct package **pkg)
{
	char buf[PATH_MAX+1];
	char *p, *name, *evr, *arch;

	*pkg = NULL;
	strncpy(buf, dirname, sizeof(buf));
	/* get name */
	name = buf;

	/* move backward */
	/* get arch */
	for(p = buf+strlen(buf); *p != '.' && p >= buf; p--) ;
	if (p <= buf)
		return vztt_error(VZT_CANT_PARSE, 0,
			"Can't find arch : %s", dirname);
	arch = p + 1;
	*(p--) = '\0';

	/* skip last '-' */
	for(; *p != '-' && p >= buf; p--) ;
	if (p <= buf)
		return vztt_error(VZT_CANT_PARSE, 0,
			"Can't find release : %s", dirname);
	p--;
	/* get evr */
	for(; *p != '-' && p >= buf; p--) ;
	if (p <= buf)
		return vztt_error(VZT_CANT_PARSE, 0,
			"Can't find EVR : %s", dirname);
	evr = p + 1;
	*p = '\0';

	/* create new */
	if ((*pkg = create_structp(name, arch, evr, NULL)) == NULL) {
		vztt_logger(0, errno, "Can't alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	return 0;
}

/* find (struct package *) in list<struct package *>
 for name, [epoch], version, release and arch (if arch is defined) */
struct package_list_el * env_compat_package_find_nevra(
		struct package_list *packages,
		struct package *pkg)
{
	struct package_list_el *i;

	/* yum mix epoch 0 and epoch None */
	char *evr0, *evr1;
	if (pkg->evr[0] == '0' && pkg->evr[1] == ':')
		evr0 = pkg->evr + 2;
	else
		evr0 = pkg->evr;

	for (i = packages->tqh_first; i != NULL; i = i->e.tqe_next) {
		/* compare name & arch */
		if (cmp_pkg(i->p, pkg))
			continue;

		/* check evr:
		yum mix two cases: epoch not defined and epoch is 0 */
		if (i->p->evr[0] == '0' && i->p->evr[1] == ':')
			evr1 = i->p->evr + 2;
		else
			evr1 = i->p->evr;
		if (strcmp(evr0, evr1) == 0)
			return i;
	}
	return NULL;
}

/* Dummy function - always return error */
int env_compat_create_init_cache(
		struct Transaction *pm,
		struct string_list *packages0,
		struct string_list *packages1,
		struct string_list *packages,
		struct package_list *installed)
{
	vztt_logger(1, 0, "mid-pre-install script execution not supported");

	return VZT_TMPL_BROKEN;
}

/* Dummy function - always return error */
int env_compat_create_post_init_cache(
		struct Transaction *pm,
		struct string_list *packages0,
		struct string_list *packages1,
		struct string_list *packages,
		struct package_list *installed)
{
	vztt_logger(1, 0, "mid-post-install script execution not supported");

	return VZT_TMPL_BROKEN;
}

int env_compat_get_group_list(struct Transaction *pm, struct group_list *ls)
{
	vztt_logger(0, errno, "operations with groups are not supported by apt");
	return VZT_UNSUPPORTED_COMMAND;
}

int env_compat_get_group_info(
		struct Transaction *pm,
		const char *group,
		struct group_info *group_info)
{
	vztt_logger(0, errno, "operations with groups are not supported by this environment");
	return VZT_UNSUPPORTED_COMMAND;
}

int env_compat_run_local(
		struct Transaction *pm,
		pm_action_t cmd,
		struct string_list *packages,
		struct package_list *added,
		struct package_list *removed)
{
	int rc;
	struct string_list files;
	struct string_list_el *i;

	string_list_init(&files);

	for (i = packages->tqh_first; i != NULL; i = i->e.tqe_next)
		string_list_add(&files, i->s);

	rc = pm_modify(pm, cmd, &files, added, removed);
	string_list_clean(&files);

	return rc;
}
