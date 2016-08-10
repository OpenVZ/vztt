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
 * dpkg/apt wrapper module
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
#include <getopt.h>
#include <libgen.h>

#include "vzcommon.h"
#include "vztt_error.h"
#include "tmplset.h"
#include "transaction.h"
#include "apt.h"
#include "util.h"
#include "md5.h"
#include "vztt.h"
#include "env_compat.h"
#include "progress_messages.h"

#define DEB_EXT ".deb"

int apt_init(struct Transaction *pm);
int apt_clean(struct Transaction *pm);
int apt_get_install_pkg(struct Transaction *apt, struct package_list *packages);
int apt_update_metadata(struct Transaction *pm, const char *name);
int apt_action(
		struct Transaction *pm,
		pm_action_t action,
		struct string_list *packages);
int apt_create_root(char *dir);
int apt_find_pkg_area(
		struct Transaction *pm,
		struct package *pkg);
int apt_find_pkg_area2(
		struct Transaction *pm,
		struct package *pkg);
int apt_find_pkg_area_ex(
		struct Transaction *pm,
		struct package *pkg,
		char *dir,
		size_t size);
int apt_get_int_pkgname(struct package *pkg, char *name, int size);
void apt_get_short_pkgname(struct package *pkg, char *name, int size);
int apt_fix_pkg_db(struct Transaction *pm);
int apt_is_std_pkg_area(const char *pkgdir, struct package *pkg);
int apt_ver_cmp(struct Transaction *pm, const char * a, const char * b, int *eval);
char *apt_os2pkgarch(const char *osarch);
int apt_find_pkg_in_cache(
		struct Transaction *pm,
		const char *nva,
		char *path,
		size_t size);
int apt_pkg_cmp(const char *pkg, struct package *p);
int apt_remove_deb(
		struct Transaction *pm,
		struct string_list *packages,
		struct package_list *remains,
		struct package_list *removed);
int apt_run_local(
		struct Transaction *pm,
		pm_action_t command,
		struct string_list *packages,
		struct package_list *added,
		struct package_list *removed);
int apt_create_init_cache(
		struct Transaction *pm,
		struct string_list *packages0,
		struct string_list *packages1,
		struct string_list *packages,
		struct package_list *installed);
int apt_create_post_init_cache(
		struct Transaction *pm,
		struct string_list *packages0,
		struct string_list *packages1,
		struct string_list *packages,
		struct package_list *installed);
int apt_create_cache(
		struct Transaction *pm,
		struct string_list *packages0,
		struct string_list *packages1,
		struct string_list *packages,
		struct package_list *installed);
int apt_clean_local_cache(struct Transaction *pm);
int apt_get_info(
		struct Transaction *pm, 
		const char *package, 
		struct pkg_info_list *ls);
int deb_get_info(
		struct Transaction *pm, 
		const char *package, 
		struct pkg_info_list *ls);
int apt_remove_local_caches(struct Transaction *pm, char *reponame);
int apt_last_repair_fetch(
		struct Transaction *pm,
		struct package *pkg,
		const char *repair_mirror);
int apt_vzttproxy_fetch(
		struct Transaction *pm,
		struct package *pkg) { return VZT_INTERNAL; }
struct package_list_el * apt_package_find_nevra(
		struct package_list *packages,
		struct package *pkg);
int apt_clone_metadata(
		struct Transaction *pm, 
		char *sname, 
		char *dname);
int apt_clean_metadata_symlinks(
		struct Transaction *pm, 
		char *name);
int apt_parse_vzdir_name(char *dirname, struct package **pkg);

/* creation */
int apt_create(struct Transaction **pm)
{
	*pm = (struct Transaction *)malloc(sizeof(struct AptTransaction));
	if (*pm == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory\n");
		return VZT_CANT_ALLOC_MEM;
	}
	memset((void *)(*pm), 0, sizeof(struct AptTransaction));
	/* set wrapper functions */
	(*pm)->pm_init = apt_init;
	(*pm)->pm_clean = apt_clean;
	(*pm)->pm_get_install_pkg = apt_get_install_pkg;
	(*pm)->pm_update_metadata = apt_update_metadata;
	(*pm)->pm_action = apt_action;
	(*pm)->pm_create_root = apt_create_root;
	(*pm)->pm_find_pkg_area = apt_find_pkg_area;
	(*pm)->pm_find_pkg_area2 = apt_find_pkg_area2;
	(*pm)->pm_find_pkg_area_ex = apt_find_pkg_area_ex;
	(*pm)->pm_get_int_pkgname = apt_get_int_pkgname;
	(*pm)->pm_get_short_pkgname = apt_get_short_pkgname;
	(*pm)->pm_fix_pkg_db = apt_fix_pkg_db;
	(*pm)->pm_is_std_pkg_area = apt_is_std_pkg_area;
	(*pm)->pm_ver_cmp = apt_ver_cmp;
	(*pm)->pm_os2pkgarch = apt_os2pkgarch;
	(*pm)->pm_find_pkg_in_cache = apt_find_pkg_in_cache;
	(*pm)->pm_pkg_cmp = apt_pkg_cmp;
	(*pm)->pm_remove_pkg = apt_remove_deb;
	(*pm)->pm_run_local = apt_run_local;
	(*pm)->pm_create_init_cache = apt_create_init_cache;
	(*pm)->pm_create_post_init_cache = apt_create_post_init_cache;
	(*pm)->pm_create_cache = apt_create_cache;
	(*pm)->pm_clean_local_cache = apt_clean_local_cache;
	(*pm)->pm_tmpl_get_info = apt_get_info;
	(*pm)->pm_ve_get_info = deb_get_info;
	(*pm)->pm_remove_local_caches = apt_remove_local_caches;
	(*pm)->pm_last_repair_fetch = apt_last_repair_fetch;
	(*pm)->pm_vzttproxy_fetch = apt_vzttproxy_fetch;
	(*pm)->pm_package_find_nevra = apt_package_find_nevra;
	(*pm)->pm_clean_metadata_symlinks = apt_clean_metadata_symlinks;
	(*pm)->pm_clone_metadata = apt_clone_metadata;
	(*pm)->pm_parse_vzdir_name = apt_parse_vzdir_name;
	(*pm)->pm_get_group_list = env_compat_get_group_list;
	(*pm)->pm_get_group_info = env_compat_get_group_info;
	(*pm)->datadir = PM_DATA_SUBDIR;
	(*pm)->pm_type = DPKG;

	return 0;
}




/* initialize */
int apt_init(struct Transaction *pm)
{
	int rc;
	char path[PATH_MAX+1];
	struct AptTransaction *apt = (struct AptTransaction *)pm;

	apt->apt_conf = NULL;
	apt->sources = NULL;
	apt->preferences = NULL;
	string_list_init(&apt->dpkg_options);

	/* create mandatory local cache tree */
	snprintf(path, sizeof(path), "%s/%s/lists/partial", \
		pm->basedir, pm->datadir);
	if (access(path, F_OK)) {
		if ((rc = create_dir(path))) {
			vztt_logger(0, rc, "Can not create %s", path);
			return VZT_CANT_CREATE;
		}
	}
	snprintf(path, sizeof(path), "%s/%s/archives/partial", \
		pm->basedir, pm->datadir);
	if (access(path, F_OK)) {
		if ((rc = create_dir(path))) {
			vztt_logger(0, rc, "Can not create %s", path);
			return VZT_CANT_CREATE;
		}
	}

	return 0;
}

/* cleanup */
int apt_clean(struct Transaction *pm)
{
	struct AptTransaction *apt = (struct AptTransaction *)pm;

	string_list_clean(&apt->dpkg_options);
	VZTT_FREE_STR(apt->apt_conf);
	VZTT_FREE_STR(apt->sources);
	VZTT_FREE_STR(apt->preferences);
	
	return 0;
}


/* Read rpm package(s) data from <fp>, parse and
   put into struct package * list <ls> */ 
static int read_deb(FILE *fp, void *data)
{
	char buf[PATH_MAX+1];
	struct package *pkg;
	struct package_list *packages = (struct package_list *)data;
	int rc;
	char *install = "install ";
	char *ptr;

	while(fgets(buf, sizeof(buf), fp)) {
		if (strncmp(buf, install, strlen(install)))
			continue;
		for (ptr=buf; *ptr && *ptr!='='; ptr++) ;
		if (!*ptr) continue;
		ptr++;
		if ((rc = parse_p(ptr, &pkg)))
			return rc;
		/* parse_p create <pkg>, add this pointer to list */
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
int apt_get_install_pkg(struct Transaction *apt, struct package_list *packages)
{
	int rc = 0;
	struct string_list args;
	struct string_list envs;
	struct package_list_el *p;
	char buf[PATH_MAX+1];

	string_list_init(&args);
	string_list_init(&envs);

	/* dpkg parameters */
	string_list_add(&args, "--show");
	string_list_add(&args, "--admindir");
	snprintf(buf, sizeof(buf), "%s/var/lib/dpkg", apt->rootdir);
	string_list_add(&args, buf);
	string_list_add(&args, "--showformat");
	/* attn: only _one_ space as delimiter, 
	string can not start from space */
	snprintf(buf, sizeof(buf), "${Status}=${Package} ${Version} "\
		"${Architecture} ${Description;-%d}\\n", DPKG_DESCRIPTION_LEN);
	string_list_add(&args, buf);

	/* run cmd from chroot environment */
	rc = run_from_chroot2(DPKG_QUERY_BIN, apt->envdir, apt->debug, \
		apt->ign_pm_err, &args, &envs, apt->osrelease, \
		read_deb, (void *)packages);

	/* free mem */
	string_list_clean(&args);
	string_list_clean(&envs);

	vztt_logger(4, 0, "Installed packages are:");
	/* copy to out list */
	for (p = packages->tqh_first; p != NULL; p = p->e.tqe_next)
		vztt_logger(4, 0, "\t%s %s %s", p->p->name, p->p->evr, p->p->arch);

	return rc;

}

/* create temporary file sources.list file for apt */
static int apt_create_sources(struct AptTransaction *apt)
{
	char path[PATH_MAX+1];
	int td;
	FILE *fd;
	struct repo_rec *r;
	int is_file;
	struct _url u;
	char *file_p = "file:/";
	char *copy_p = "copy:/";
	int rc;
	char url[2*PATH_MAX];
	char *apt_vars[] = {"$(ARCH)", NULL};

	if (repo_list_empty(&apt->repositories)) {
		vztt_logger(0, 0, "repositories not defined");
		return VZT_INTERNAL;
	}

	/* create sources */
	snprintf(path, sizeof(path), "%s/sources.XXXXXX", apt->tmpdir);
	if ((td = mkstemp(path)) == -1) {
		vztt_logger(0, errno, "mkstemp(%s) error", path);
		return VZT_CANT_CREATE;
	}
	if ((apt->sources = strdup(path)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		close(td);
		return VZT_CANT_ALLOC_MEM;
	}

	if ((fd = fdopen(td, "w")) == NULL) {
		vztt_logger(0, errno, "fdopen(%s) error", path);
		return VZT_CANT_OPEN;
	}

	rc = 0;
	for (r = apt->repositories.tqh_first; r != NULL; r = r->e.tqe_next) {
		is_file = 0 ;
		/* use replacement in url_map (host1.domain1 -> host2.domain2) */
		prepare_url(apt->url_map, r->url, url, sizeof(url));

		/* check non-apt $* variable in url */
		if ((rc = pm_check_url(url, apt_vars, apt->force)))
			break;

		if (strncmp(url, file_p, strlen(file_p)) == 0) {
			memcpy((void *)url, (void *)copy_p, strlen(copy_p));
			is_file = 1 ;
		}

		fprintf(fd, "deb %s\n", url);
		if (apt->vzttproxy && !is_file) {
			if (parse_url(url, &u) == 0)
				fprintf(fd, "deb %s/%s/%s\n", \
					apt->vzttproxy, u.server, u.path);
		}
	}
	fclose(fd);
	close(td);
	if (rc)
		return rc;
	vztt_logger(2, 0, "Temporary sources file %s was created", apt->sources);

	return 0;
}

/* remove temporary sources.list file */
static int apt_remove_sources(struct AptTransaction *apt)
{
	if (apt->sources == NULL)
		return 0;

	unlink(apt->sources);
	free((void *)apt->sources);
	apt->sources = NULL;
	return 0;
}

/* create apt preferences file for excludes */
static int apt_create_preferences(struct AptTransaction *apt, FILE *cfd)
{
	char path[PATH_MAX+1];
	int td;
	FILE *fd;
	struct string_list_el *e;
	struct stat st;

	/* Create preferences.d required by apt 0.8.13 */
	snprintf(path, sizeof(path), "%s/preferences.d", apt->tmpdir);
	if (stat(path, &st) == -1 && errno == ENOENT && mkdir(path, 0755) != 0) {
		vztt_logger(0, errno, "mkdir(%s) error", path);
		return VZT_CANT_CREATE;
	}

	if (string_list_empty(&apt->exclude)) {
		fprintf(cfd, "     Preferences \"preferences\";\n");
		return 0;
	}

	/* create temporary yum confug */
	snprintf(path, sizeof(path), "%s/preferences.XXXXXX", apt->tmpdir);
	if ((td = mkstemp(path)) == -1) {
		vztt_logger(0, errno, "mkstemp(%s) error", path);
		return VZT_CANT_CREATE;
	}
	apt->preferences = strdup(path);
	if (apt->preferences == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		close(td);
		return VZT_CANT_ALLOC_MEM;
	}
	if ((fd = fdopen(td, "w")) == NULL) {
		vztt_logger(0, errno, "fdopen(%s) error", apt->preferences);
		return VZT_CANT_OPEN;
	}

	for (e = apt->exclude.tqh_first; e != NULL; e = e->e.tqe_next) {
		fprintf(fd, "Package:  %s\n", e->s);
		fprintf(fd, "Pin: origin \"\"\n");
		fprintf(fd, "Pin-Priority: 1000\n\n");
	}

	fprintf(cfd, "     Preferences \"%s\";\n", apt->preferences);
	fclose(fd);
	close(td);
	vztt_logger(2, 0, "Temporary preferences file %s was created", apt->preferences);

	return 0;
}

/* create temporary apt.conf */
static int apt_create_config(struct AptTransaction *apt)
{
	char path[PATH_MAX+1];
	int td;
	FILE *fd;
	struct string_list_el *p;
	int qlevel = 0;
	char dflag[10];
	int rc;

	if ((rc = apt_create_sources(apt)))
		return rc;

	/* create temporary yum confug */
	snprintf(path, sizeof(path), "%s/apt_conf.XXXXXX", apt->tmpdir);
	if ((td = mkstemp(path)) == -1) {
		vztt_logger(0, errno, "mkstemp(%s) error", path);
		return VZT_CANT_CREATE;
	}
	apt->apt_conf = strdup(path);
	if (apt->apt_conf == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		close(td);
		return VZT_CANT_ALLOC_MEM;
	}

	if ((fd = fdopen(td, "w")) == NULL) {
		vztt_logger(0, errno, "fdopen(%s) error", apt->apt_conf);
		return VZT_CANT_OPEN;
	}
	path[0] = '\0';
	if (apt->tmpldir) {
		strncpy(path, apt->tmpldir, sizeof(path));
		strncat(path, "/", sizeof(path)-strlen(path)-1);
	}
	if (apt->basesubdir)
		strncat(path, apt->basesubdir, sizeof(path)-strlen(path)-1);

	/* qlevel: 0 - normal, 1, 2 - low, 3 - very low */
	strcpy(dflag,"false");
	if (apt->quiet)
		qlevel = 3;
	else {
		/*
		0 - 3
		1 - 2
		2 - 1
		3 - 1
		4 - 0
		5 - 0 + dflag == true
		*/
		if ( apt->debug == 0 ) qlevel = 3;
		/* qlevel = 2 does not show 'unmet' error */
		else if ( apt->debug == 1 ) qlevel = 1;
		else if ( apt->debug <= 3 ) qlevel = 0;
		else if ( apt->debug >= 4 ) {
			qlevel = 0;
			strcpy(dflag, "true");
		}
	}

	/* content */
	fprintf(fd, "quiet \"%d\";\n", qlevel);
	/* Options for APT in general */
	fprintf(fd, "APT\n{\n");
	if (strcmp(apt->pkgarch, ARCH_AMD64) == 0)
		if (apt->tdata->base->multiarch == 1)
			fprintf(fd, "  Architectures { \"" ARCH_AMD64 "\"; \"" ARCH_I386 "\" };");
		else
			fprintf(fd, "  Architectures { \"" ARCH_AMD64 "\" };");
	else
		fprintf(fd, "  Architectures { \"" ARCH_I386 "\" };");

	fprintf(fd, "  Build-Essential \"build-essential\";\n");
	/* Options for apt-get */
	fprintf(fd, "  Get\n  {\n");
	if (!EMPTY_CTID(apt->ctid))
		fprintf(fd, "     ctid %s; // added by swsoft\n", apt->ctid);
	if (apt->rootdir)
		fprintf(fd, "     root \"%s\"; // added by swsoft\n", \
			apt->rootdir);
	if (apt->outfile)
		fprintf(fd, "     outfile \"%s\"; // added by swsoft\n", \
			apt->outfile);
	fprintf(fd, "     Arch-Only \"false\";\n");
	/* skip Download-Only - use default value, 
	to change via --download-only apt-get option */
	fprintf(fd, "     Simulate \"false\";\n");
	fprintf(fd, "     Assume-Yes \"true\"; // swsoft\n");
	fprintf(fd, "     Force-Yes \"false\"; // I would never set this.\n");
	fprintf(fd, "     Fix-Broken \"true\"; // swsoft\n");
	fprintf(fd, "     Fix-Missing \"%s\";\n", (apt->force)?"true":"false");
	fprintf(fd, "     Show-Upgraded \"true\"; // swsoft  - used for status()\n");
	fprintf(fd, "     Show-Versions \"false\";\n");
	fprintf(fd, "     Upgrade \"true\";\n");
	fprintf(fd, "     Print-URIs \"false\";\n");
	fprintf(fd, "     Compile \"false\";\n");
	fprintf(fd, "     Download \"%s\";\n", \
		(apt->data_source == OPT_DATASOURCE_LOCAL)?"false":"true");
	fprintf(fd, "     Purge \"false\";\n");
	fprintf(fd, "     List-Cleanup \"true\";\n");
	fprintf(fd, "     ReInstall \"false\";\n");
	fprintf(fd, "     Trivial-Only \"false\";\n");
	fprintf(fd, "     Remove \"true\";\n");
	fprintf(fd, "     Only-Source \"\";\n");
	fprintf(fd, "     Diff-Only \"false\";\n");
	fprintf(fd, "     Tar-Only \"false\";\n");
	fprintf(fd, "  };\n\n  Cache\n  {\n");
	fprintf(fd, "     Important \"false\";\n");
	fprintf(fd, "     AllVersions \"true\"; // swsoft - to show last package only\n");
	fprintf(fd, "     GivenOnly \"false\";\n");
	fprintf(fd, "     RecurseDepends \"false\";\n");
	fprintf(fd, "     ShowFull \"false\";\n");
	fprintf(fd, "     Generate \"true\";\n");
	fprintf(fd, "     NamesOnly \"false\";\n");
	fprintf(fd, "     AllNames \"false\";\n");
	fprintf(fd, "     Installed \"false\";\n");
	fprintf(fd, "     ShowNVA \"true\";  // added by swsoft - show version + arch for pkgnames\n");
	fprintf(fd, "  };\n\n");
	/* Some general options */
	fprintf(fd, "  Ignore-Hold \"false\";\n");
	fprintf(fd, "  Clean-Installed \"true\";\n");
	fprintf(fd, "  Immediate-Configure \"true\";      // DO NOT turn this off, see the man page\n");
	fprintf(fd, "  Force-LoopBreak \"false\";         // DO NOT turn this on, see the man page\n");
	fprintf(fd, "  Default-Release \"\";\n");
	fprintf(fd, "};\n\n");
	/* Options for the downloading routines */
	fprintf(fd, "Acquire\n{\n");
	fprintf(fd, "  Queue-Mode \"host\";       // host|access\n");
	fprintf(fd, "  Retries \"0\";\n");
	fprintf(fd, "  Source-Symlinks \"true\";\n");
	fprintf(fd, "};\n\n");
	/* Directory layout */
	fprintf(fd, "Dir \"\" // swsoft\n");
	fprintf(fd, "{\n");
	fprintf(fd, "  EnvDir \"%s\";  // swsoft\n", apt->envdir);
	/* Location of the state dir */
	fprintf(fd, "  State \"%s/" PM_DATA_SUBDIR "/\"  // swsoft\n", path);
	fprintf(fd, "  {\n");
	fprintf(fd, "     Lists \"lists/\";\n");
	fprintf(fd, "     xstatus \"xstatus\";\n");
	fprintf(fd, "     userstatus \"status.user\";\n");
	fprintf(fd, "     status \"%s/var/lib/dpkg/status\"; // swsoft\n", apt->rootdir);
	fprintf(fd, "     cdroms \"cdroms.list\";\n");
	fprintf(fd, "  };\n\n");
	/* Location of the cache dir */
	fprintf(fd, "  Cache \"%s/" PM_DATA_SUBDIR "/\" // swsoft\n", path);
	fprintf(fd, "  {\n");
	fprintf(fd, "     Archives \"archives/\";\n");
	fprintf(fd, "     srcpkgcache \"srcpkgcache.bin\";\n");
	fprintf(fd, "     pkgcache \"pkgcache.bin\";     \n");
	fprintf(fd, "  };\n\n");
	/* Config files */
	fprintf(fd, "  Etc \"%s\" // swsoft\n", apt->tmpdir);
	fprintf(fd, "  {\n");
	fprintf(fd, "     SourceList \"%s\"; // swsoft\n", apt->sources);
	fprintf(fd, "     Main \"apt.conf\";\n");
	if ((rc = apt_create_preferences(apt, fd))) {
		fclose(fd);
		close(td);
		return rc;
	}
//	fprintf(fd, "     Preferences \"preferences\";\n");
	fprintf(fd, "     Parts \"apt.conf.d/\";\n");
	fprintf(fd, "  };\n");
	/* Locations of binaries */
	fprintf(fd, "  Bin {\n");
	fprintf(fd, "     methods \"/usr/lib/apt/methods/\"; // swsoft\n");
	fprintf(fd, "     gzip \"/bin/gzip\"; // swsoft\n");
	fprintf(fd, "     dpkg \"/usr/bin/dpkg\"; // swsoft\n");
	fprintf(fd, "     dpkg-source \"%s/usr/bin/dpkg-source\"; // swsoft\n", apt->envdir);
	fprintf(fd, "     dpkg-buildpackage \"%s/usr/bin/dpkg-buildpackage\"; // swsoft\n", apt->envdir);
	fprintf(fd, "     apt-get \"%s/usr/bin/apt-get\"; // swsoft\n", apt->envdir);
	fprintf(fd, "     apt-cache \"%s/usr/bin/apt-cache\"; // swsoft\n", apt->envdir);
	fprintf(fd, "  };\n");
	fprintf(fd, "};\n\n");
	fprintf(fd, "DPkg\n");
	fprintf(fd, "{\n");
	/* swsoft --abort-after=1 ? */
	fprintf(fd, "   Options {");
	if (!EMPTY_CTID(apt->ctid))
		fprintf(fd, "\"--veid=%s\";", apt->ctid);
	fprintf(fd, "\"--root=%s\";\"--envdir=%s\";\"--vz_template=%s\";", \
			apt->rootdir, apt->envdir, apt->tmpldir);
	if (apt->force_openat || apt->vzfs_technologies)
		fprintf(fd, "\"--osbasedir=%s\";", apt->basesubdir);
	if (apt->force_openat)
		fprintf(fd, "\"--openat-force\";");
	else if (apt->vzfs_technologies)
		fprintf(fd, "\"--vz_technologies=%lu\";", apt->vzfs_technologies);
	if (apt->interactive)
		fprintf(fd, "\"--interactive\";");
	/* and add dpkg options */
	for (p = apt->dpkg_options.tqh_first; p != NULL; p = p->e.tqe_next)
		fprintf(fd, "\"%s\";", p->s);
	fprintf(fd, "};\n");

	fprintf(fd, "   Run-Directory \"/\";\n");
	fprintf(fd, "   Pre-Install-Pkgs ;\n");
	fprintf(fd, "   FlushSTDIN \"true\";\n");
	fprintf(fd, "   MaxBytes 1024; // swsoft - max size of one arg \n");
	fprintf(fd, "   MaxArgs 1024;   // swsoft - max args number\n");
	fprintf(fd, "}\n\n");
	fprintf(fd, "Debug \n");
	fprintf(fd, "{\n");
	fprintf(fd, "  pkgProblemResolver \"%s\";\n", dflag);
	fprintf(fd, "  pkgAcquire \"%s\";\n", dflag);
	fprintf(fd, "  pkgAcquire::Worker \"%s\";\n", dflag);
	fprintf(fd, "  pkgDPkgPM \"false\"; // it is equal simulate option\n");
	fprintf(fd, "  pkgOrderList \"%s\";\n", dflag);
  
	fprintf(fd, "  pkgInitialize \"%s\";   // This one will dump the configuration space\n", dflag);
	fprintf(fd, "  NoLocking \"%s\";\n", dflag);
	fprintf(fd, "  Acquire::Ftp \"%s\";    // Show ftp command traffic\n", dflag);
	fprintf(fd, "  Acquire::Http \"%s\";   // Show http command traffic\n", dflag);
	fprintf(fd, "  aptcdrom \"%s\";        // Show found package files\n", dflag);
	fprintf(fd, "  IdentCdrom \"%s\";\n", dflag);
	fprintf(fd, "}\n");

	fclose(fd);
	close(td);
	vztt_logger(2, 0, "Temporary apt config %s was created", apt->apt_conf);

	return 0;
}

/* remove temporary apt.conf */
static int apt_remove_config(struct AptTransaction *apt)
{
	apt_remove_sources(apt);
	if (apt->apt_conf == NULL)
		return 0;
	unlink(apt->apt_conf);
	free((void *)apt->apt_conf);
	apt->apt_conf = NULL;
	return 0;
}

/* run apt-* command */
static int apt_run(
		struct AptTransaction *apt,
		const char *cmd, \
		const char *action, \
		struct string_list *packages)
{
	int rc;
	struct string_list args;
	struct string_list envs;
	struct string_list_el *o;

	/* Empty packages list, special case of app template: #PSBM-26883
	   Not packages-related commands should be executed with packages NULL
	 */
	string_list_init(&args);
	string_list_init(&envs);

	if ((rc = apt_create_config(apt)))
		return rc;

	/* config file */
	string_list_add(&args, "-c");
	string_list_add(&args, apt->apt_conf);
	if ( (strcmp(cmd, APT_GET_BIN) == 0) && ((apt->download_only) || (apt->test)))
		string_list_add(&args, "--download-only");
	/* additional options */
	for (o = apt->options.tqh_first; o != NULL; o = o->e.tqe_next) {
		string_list_add(&args, "-o");
		string_list_add(&args, o->s);
	}

	/* add command */
	string_list_add(&args, (char *)action);

	if (packages) {
		/* to add packages into arguments */
		for (o = packages->tqh_first; o != NULL; o = o->e.tqe_next)
			if (*o->s != '*')
				string_list_add(&args, o->s);
	}

	/* add proxy in environments */
	if ((rc = add_proxy_env(&apt->http_proxy, HTTP_PROXY, &envs)))
		return rc;
	if ((rc = add_proxy_env(&apt->ftp_proxy, FTP_PROXY, &envs)))
		return rc;
	if ((rc = add_proxy_env(&apt->https_proxy, HTTPS_PROXY, &envs)))
		return rc;

	/* add templates environments too */
	if ((rc = add_tmpl_envs(apt->tdata, &envs)))
		return rc;

	/* run cmd from chroot environment */
	if ((rc = run_from_chroot((char *)cmd, apt->envdir, apt->debug, \
			apt->ign_pm_err, &args, &envs, apt->osrelease)))
		return rc;

	apt_remove_config(apt);

	/* free mem */
	string_list_clean(&args);
	string_list_clean(&envs);

	return 0;
}

/* run dpkg command */
static int dpkg_run(struct AptTransaction *apt, char *cmd, struct string_list *args)
{
	int rc;
	struct string_list envs;

	string_list_init(&envs);

	/* run cmd from chroot environment */
	rc = run_from_chroot(cmd, apt->envdir, apt->debug, 
		apt->ign_pm_err, args, &envs, apt->osrelease);

	return rc;
}

/* update apt metadata */
int apt_update_metadata(struct Transaction *pm, const char *name)
{
	int rc;
	char path[PATH_MAX+1];
	char *of = pm->outfile;
	int td;
	struct AptTransaction *apt = (struct AptTransaction *)pm;
	int data_source = pm->data_source;
	char progress_stage[PATH_MAX];

	snprintf(progress_stage, sizeof(progress_stage),
		PROGRESS_PROCESS_METADATA, name);

	progress(progress_stage, 0, pm->progress_fd);

	/* update package database */
	/* it is not needs to reset rootdir - metadata 
	will created in template area */
	pm->data_source = OPT_DATASOURCE_REMOTE;
	string_list_add(&apt->options, "APT::Get::List-Cleanup=false");
	rc = apt_run(apt, APT_GET_BIN, "update", NULL);
	pm->data_source = data_source;
	if (rc)
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

	/* get full packages list and update metadata */
	pm->data_source = OPT_DATASOURCE_REMOTE;
	rc = apt_run(apt, APT_CACHE_BIN, "pkgnames", NULL);
	pm->data_source = data_source;
	if (rc)
		return rc;

	/* and save metadata */
	if ((rc = pm_save_metadata(pm, name)))
		return rc;

	free((void *)pm->outfile);
	pm->outfile = of;

	progress(progress_stage, 100, pm->progress_fd);

	return 0;
}

/* run apt transaction */
int apt_action(
		struct Transaction *pm,
		pm_action_t action,
		struct string_list *packages)
{
	char *bin = APT_GET_BIN; /* apt-get by default */
	char *cmd;
	int rc = 0;
	struct AptTransaction *apt = (struct AptTransaction *)pm;
	int save_download_only = apt->download_only;
	char progress_stage[PATH_MAX];

	/* add command */
	switch(action)
	{
		case VZPKG_INSTALL:
		case VZPKG_LOCALINSTALL:
		case VZPKG_LOCALUPDATE:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_INSTALL);
			cmd = "install";
			string_list_add(&apt->dpkg_options, "--force-configure-any");
			string_list_add(&apt->dpkg_options, "--force-confdef");
			string_list_add(&apt->dpkg_options, "--force-confold");
			if (apt->force) {
				string_list_add(&apt->dpkg_options, "--force-depends");
				string_list_add(&apt->dpkg_options, "--force-conflicts");
				string_list_add(&apt->dpkg_options, "--force-overwrite");
				string_list_add(&apt->dpkg_options, "--force-overwrite-dir");
//				string_list_add(&apt->options, "APT::Get::Force-Broken=true");
			}
			break;
		case VZPKG_REMOVE:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_REMOVE);
			cmd = "remove";
			string_list_add(&apt->dpkg_options, "--force-remove-reinstreq");
			string_list_add(&apt->dpkg_options, "--force-remove-essential");
			if (apt->force) {
				string_list_add(&apt->dpkg_options, "--force-depends");
				string_list_add(&apt->options, "APT::Get::Force-Broken=true");
			}
			if (!apt->depends)
				string_list_add(&apt->options, "APT::Get::Resolve=false");
			break;
		case VZPKG_UPGRADE:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_UPGRADE);
			cmd = "dist-upgrade";
			string_list_add(&apt->dpkg_options, "--force-configure-any");
			string_list_add(&apt->dpkg_options, "--force-confdef");
			/* do not use confold: dpkg will keep 
			magic symlinks to old template area */
			string_list_add(&apt->dpkg_options, "--force-confnew");
			if (apt->force) {
				string_list_add(&apt->dpkg_options, "--force-depends");
				string_list_add(&apt->dpkg_options, "--force-conflicts");
				string_list_add(&apt->dpkg_options, "--force-overwrite");
				string_list_add(&apt->dpkg_options, "--force-overwrite-dir");
			}
			break;
		case VZPKG_LIST:
			if (apt->debug > 1)
				snprintf(progress_stage, sizeof(progress_stage), "%s%s",
					PROGRESS_PKGMAN_PACKAGE_MANAGER,
					PROGRESS_PKGMAN_LIST);
			bin = APT_CACHE_BIN; /* use apt-cache */
			cmd = "pkgnames";
			break;
		case VZPKG_AVAIL:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_AVAILABLE);
			bin = APT_CACHE_BIN; /* use apt-cache */
			cmd = "avail";
			break;
		case VZPKG_FETCH:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_FETCH);
			/* load packages with dependencies resolving, 
			   but ignore conficts */
			cmd = "install";
			string_list_add(&apt->options, 
				"APT::Get::Force-Broken=true");
			apt->download_only = 1;
			break;
		case VZPKG_GET:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_FETCH);
			/* load only specified packages, without any resolving, 
			   ignore all conficts */
			cmd = "install";
			string_list_add(&apt->options, 
				"APT::Get::Force-Broken=true");
			string_list_add(&apt->options, 
				"APT::Get::Resolve=false");
			apt->download_only = 1;
			break;
		case VZPKG_CLEAN:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_CLEAN);
			cmd = "clean";
			break;
		case VZPKG_INFO:
			snprintf(progress_stage, sizeof(progress_stage), "%s%s",
				PROGRESS_PKGMAN_PACKAGE_MANAGER,
				PROGRESS_PKGMAN_INFO);
			bin = APT_CACHE_BIN; /* use apt-cache */
			cmd = "show";
			break;
		case VZPKG_GROUPINSTALL:
		case VZPKG_GROUPUPDATE:
		case VZPKG_GROUPREMOVE:
			vztt_logger(0, errno,
				"operations with groups are not supported by apt");
			rc = VZT_UNSUPPORTED_COMMAND;
			goto cleanup;
		case VZPKG_UPDATE:
		default:
			if (apt->expanded)
			{
				snprintf(progress_stage, sizeof(progress_stage), "%s%s",
					PROGRESS_PKGMAN_PACKAGE_MANAGER,
					PROGRESS_PKGMAN_UPGRADE);
				cmd = "dist-upgrade";
			}
			else
			{
				snprintf(progress_stage, sizeof(progress_stage), "%s%s",
					PROGRESS_PKGMAN_PACKAGE_MANAGER,
					PROGRESS_PKGMAN_UPDATE);
				cmd = "upgrade";
			}
			string_list_add(&apt->dpkg_options, "--force-configure-any");
			string_list_add(&apt->dpkg_options, "--force-confdef");
			string_list_add(&apt->dpkg_options, "--force-confold");
			if (apt->force) {
				string_list_add(&apt->dpkg_options, "--force-depends");
				string_list_add(&apt->dpkg_options, "--force-conflicts");
				string_list_add(&apt->dpkg_options, "--force-overwrite");
				string_list_add(&apt->dpkg_options, "--force-overwrite-dir");
			}
			break;
	}

	progress(progress_stage, 0, apt->progress_fd);

	rc = apt_run(apt, bin, cmd, packages);

	progress(progress_stage, 100, apt->progress_fd);

cleanup:
	string_list_clean(&apt->options);
	string_list_clean(&apt->dpkg_options);
	apt->download_only = save_download_only;

	return rc;
}

/* create root for dpkg in <dir> */
int apt_create_root(char *dir)
{
	char path[PATH_MAX];
	int rc, fd;
	char *str;

        /* create mandatory dirs */
	snprintf(path, sizeof(path), "%s/var", dir);
	if (access(path, F_OK))
		if (mkdir(path, 0755)) {
			vztt_logger(0, errno, "Can not create %s directory", \
				path);
			return VZT_CANT_CREATE;
		}
	snprintf(path, sizeof(path), "%s/var/lib", dir);
	if (access(path, F_OK))
		if (mkdir(path, 0755)) {
			vztt_logger(0, errno, "Can not create %s directory", \
				path);
			return VZT_CANT_CREATE;
		}
	snprintf(path, sizeof(path), "%s/var/lib/dpkg", dir);
	if (access(path, F_OK))
		if (mkdir(path, 0755)) {
			vztt_logger(0, errno, "Can not create %s directory", \
				path);
			return VZT_CANT_CREATE;
		}

	snprintf(path, sizeof(path), "%s/var/lib/dpkg/updates", dir);
	if (access(path, F_OK) != 0)
		if (mkdir(path, 0755)) {
			vztt_logger(0, errno, "Can not create %s directory", \
				path);
			return VZT_CANT_CREATE;
		}
	snprintf(path, sizeof(path), "%s/var/lib/dpkg/info", dir);
	if (access(path, F_OK) != 0)
		if (mkdir(path, 0755)) {
			vztt_logger(0, errno, "Can not create %s directory", \
				path);
			return VZT_CANT_CREATE;
		}


	snprintf(path, sizeof(path), "%s/var/lib/dpkg/available", dir);
	if ((fd = creat(path, 0644)) < 0) {
		vztt_logger(0, errno, "Can not open %s", path);
		return VZT_CANT_OPEN;
	}
	str = "\n";
	rc = write(fd, (void *)str, strlen(str) + 1);
	close(fd);
	if (rc == -1)
		return vztt_error(VZT_SYSTEM, errno, "write()");

	snprintf(path, sizeof(path), "%s/var/lib/dpkg/status", dir);
	if ((fd = creat(path, 0644)) < 0) {
		vztt_logger(0, errno, "Can not open %s", path);
		return VZT_CANT_OPEN;
	}
	str = "Package: dpkg\nVersion: 1.9.1\nStatus: install ok installed\n" \
		"Maintainer: Parallels <wolf@parallels.com>\n" \
		"Architecture: all\n" \
		"Description: dpkg patched by Parallels\n";
	rc = write(fd, (void *)str, strlen(str) + 1);
	close(fd);
	if (rc == -1)
		return vztt_error(VZT_SYSTEM, errno, "write()");

	snprintf(path, sizeof(path), "%s/var/lib/dpkg/cmethopt", dir);
	if ((fd = creat(path, 0644)) < 0) {
		vztt_logger(0, errno, "Can not open %s", path);
		return VZT_CANT_OPEN;
	}
	str = "apt apt\n";
	rc = write(fd, (void *)str, strlen(str) + 1);
	close(fd);
	if (rc == -1)
		return vztt_error(VZT_SYSTEM, errno, "write()");

	snprintf(path, sizeof(path), "%s/var/lib/dpkg/info/dpkg.list", dir);
	if ((fd = creat(path, 0644)) < 0) {
		vztt_logger(0, errno, "Can not open %s", path);
		return VZT_CANT_OPEN;
	}
	rc = write(fd, (void *)"", 0);
	close(fd);
	if (rc == -1)
		return vztt_error(VZT_SYSTEM, errno, "write()");

	return 0;
}

/* get name of template area's directory for package */
static void apt_get_pkg_dirname(struct package *pkg, char *buf, int size)
{
	char *colon = strchr(pkg->evr, ':');

	if (colon == NULL) {
		snprintf(buf, size, "%s_%s_%s", \
			pkg->name, pkg->evr, pkg->arch);
	} else {
		/* replace : to %3a */
		*colon = '\0';
		snprintf(buf, size, "%s_%s%%3a%s_%s", \
			pkg->name, pkg->evr, colon+1, pkg->arch);
		*colon = ':';
	}
}

/* Find package directory in <area>
*/
static int apt_find_pkg(
		struct Transaction *pm,
		struct package *pkg,
		char *subdir,
		char *dir,
		size_t size)
{
	size_t i;
	/* all available arch */
	char *archs[] = {"all", "i386", "amd64", "ia64", NULL};
	char path[PATH_MAX+1];

	if (strlen(pkg->arch)) {
		apt_get_pkg_dirname(pkg, dir, size);
		snprintf(path, sizeof(path), "%s/%s/%s", \
				pm->tmpldir, subdir, dir);
		if (access(path, R_OK) == 0)
			return 1;
	} else {
		/* check all available archs */
		char *ptr = pkg->arch;
/* TODO: get archs list for basearch */

		for (i = 0; archs[i]; i++) {
			pkg->arch = archs[i];
			apt_get_pkg_dirname(pkg, dir, size);
			snprintf(path, sizeof(path), "%s/%s/%s", \
					pm->tmpldir, subdir, dir);
			if (access(path, R_OK) == 0) {
				pkg->arch = strdup(archs[i]);
				return 1;
			}
		}
		pkg->arch = ptr;
	}
	return 0;
}


/* Find package directory in EZ template area
   Parameters:
   - tmpldir :	template directory (/vz/template)
   - basesubdir : OS template subdirectory (debian/3.1/x86)
   - pkg :	package structure
   Attn: old dpkg does not keep package architecture in status file
   and pkg->arch may be empty.
   In such case this function find first directory with proper 
   pkg->name and pkg->evr and place to pkg->arch arch of foud package
*/
int apt_find_pkg_area(
		struct Transaction *pm,
		struct package *pkg)
{
	char dir[PATH_MAX];

	return apt_find_pkg(pm, pkg, pm->basesubdir, dir, sizeof(dir));
}

/* find package directory in template */
int apt_find_pkg_area2(
		struct Transaction *pm,
		struct package *pkg)
{
	char dir[PATH_MAX];

	/* 1. seek in template area */
	if (apt_find_pkg(pm, pkg, pm->basesubdir, dir, sizeof(dir)))
		return 1;
	return 0;
}

/* Find package directory in EZ template area
*/
int apt_find_pkg_area_ex(
		struct Transaction *pm,
		struct package *pkg,
		char *dir,
		size_t size)
{
	return apt_find_pkg(pm, pkg, pm->basesubdir, dir, size);
}

/* get apt package name */
int apt_get_int_pkgname(struct package *pkg, char *name, int size)
{
	if (pkg->arch && strlen(pkg->arch))
		snprintf(name, size, "%s:%s=%s", pkg->name, pkg->arch, pkg->evr);
	else
		snprintf(name, size, "%s=%s", pkg->name, pkg->evr);
	return 0;
}

/* get name without version */
void apt_get_short_pkgname(struct package *pkg, char *name, int size)
{
	strncpy(name, pkg->name, size);
}

/* get name of template area's directory for package 
in standard template format (without epoch) */
static void apt_get_std_pkg_dirname(
		struct package *pkg,
		char *dir,
		int size)
{
	char *colon = strchr(pkg->evr, ':');

	if (colon == NULL)
		snprintf(dir, size, "%s_%s_%s", \
			pkg->name, pkg->evr, pkg->arch);
	else
	{
		/* remove epoch */
		*colon = '\0';
		snprintf(dir, size, "%s_%s_%s", \
			pkg->name, pkg->evr, pkg->arch);
		*colon = ':';
	}
}

/* fix package database */
int apt_fix_pkg_db(struct Transaction *pm)
{
	return 0;
}

/* check that pkgdir is package directory for standard template */
int apt_is_std_pkg_area(const char *pkgdir, struct package *pkg)
{
	char dir[NAME_MAX+1];

	/* 2 variants : with epoch and without epoch */
	/* try with epoch */
	apt_get_pkg_dirname(pkg, dir, sizeof(dir));
	if (strcmp(pkgdir, dir) == 0)
		return 1;
	/* try without epoch */
	apt_get_std_pkg_dirname(pkg, dir, sizeof(dir));
	if (strcmp(pkgdir, dir) == 0)
		return 1;
	return 0;
}

/* compare two versions:
 *eval = 1: a is newer than b
         0: a and b are the same version
        -1: b is newer than a */
int apt_ver_cmp(struct Transaction *pm, const char * a, const char * b, int *eval)
{
	char cmd[PATH_MAX+1];
	int rc, status;

	*eval = 0;
	if (strcmp(a,b) == 0)
		return 0;

	/* run dpkg from chroot */
	snprintf(cmd, sizeof(cmd), \
		"chroot %s %s --compare-versions \"%s\" gt \"%s\"", \
		pm->envdir, DPKG_BIN, a, b);

	vztt_logger(2, 0, cmd);
	if ((status = system(cmd)) == -1) {
		vztt_logger(0, errno, "system(%s) error", cmd);
		return VZT_CANT_EXEC;
	}
	if (!WIFEXITED(status)) {
		vztt_logger(0, 0, "\"%s\" failed", cmd);
		return VZT_CMD_FAILED;
	}

	rc = WEXITSTATUS(status);
	if (rc == 0)
		*eval = 1;
	else if (rc == 1)
		*eval = -1;
	else {
		vztt_logger(0, 0, "\"%s\" return %d", cmd, rc);
		return VZT_CMD_FAILED;
	}
	return 0;
}

/* convert osarch to package arch */
char *apt_os2pkgarch(const char *osarch)
{
	if (strcmp(osarch, ARCH_X86_64) == 0)
		return strdup(ARCH_AMD64);
	else if (strcmp(osarch, ARCH_IA64) == 0)
		return strdup(ARCH_IA64);

	return strdup(ARCH_I386);
}

/* find package in local cache 
 nva - directory name in template area (name_version_arch) */
int apt_find_pkg_in_cache(
		struct Transaction *pm,
		const char *nva,
		char *path,
		size_t size)
{
	snprintf(path, size, "%s/%s/%s/archives/%s.deb", \
			pm->tmpldir, pm->basesubdir, pm->datadir,
			nva);

	if (access(path, F_OK) == 0)
		return 1;

	return 0;
}

/*
   compare conventional package name from template %packages section 
   and full name from package struct
   Specifying package names:
	name
	name=version
   0 - if success
*/
int apt_pkg_cmp(const char *pkg, struct package *p)
{
	char *ptr;

	/* 1 - name only */
	if (strcmp(pkg, p->name) == 0)
		return 0;

	/* 2 - check name=version */
	if (strncmp(pkg, p->name, strlen(p->name)))
		return 1;
	ptr = (char *)pkg + strlen(p->name);
	if (*ptr != '=')
		return 1;
	if (strcmp(++ptr, p->evr))
		return 1;

	return 0;
}

/* remove packages */
int apt_remove_deb(
		struct Transaction *pm,
		struct string_list *packages,
		struct package_list *remains,
		struct package_list *removed)
{
	int rc = 0;
	struct package_list empty;
	struct string_list args;
	struct package_list_el *p;
	struct string_list_el *i;
	int lfound = 0, mix_templates = 0;

	string_list_init(&args);

	for (i = packages->tqh_first; i != NULL; i = i->e.tqe_next) {
		lfound = 0;
		mix_templates = 0;
		/* it's may be some matches for one args */
		for (p = remains->tqh_first; p != NULL; \
				p = p->e.tqe_next) {
			/* Check that package present in other template
			   if so - skip it */
			if (apt_pkg_cmp(i->s, p->p))
			{
				mix_templates = 1;
				continue;
			}
			/* Check that package already removed
			   if so - skip it too */
			if (string_list_find(&args, p->p->name))
				continue;
			string_list_add(&args, p->p->name);
			lfound = 1;
		}
		if (!lfound && !mix_templates) {
			vztt_logger(0, 0, "Package %s is not installed", i->s);
			rc = VZT_BAD_PARAM;
			goto cleanup_1;
		}
	}

	if (string_list_size(&args))
	{
		package_list_init(&empty);
		rc = pm_modify(pm, VZPKG_REMOVE, &args, &empty, removed);
		package_list_clean_all(&empty);
	}

cleanup_1:
	string_list_clean(&args);
	return rc;
}

#define VZLREPO "vzlocalrepo"

/*
  install/update local packages: 
  create temporary apt file:/ repository, 
  convert *.deb to *.vz.deb and copy to repo,
  create temporary metadate for this repo.
*/
int apt_run_local(
		struct Transaction *pm,
		pm_action_t command,
		struct string_list *packages,
		struct package_list *added,
		struct package_list *removed)
{
	char cmd[2*PATH_MAX+1];
	char buf[STRSIZ];
	char path[PATH_MAX+1];
	int rc;
	char *ptr, *name, *vzname;
	size_t j;
	int dir_fd;
	FILE *fc, *fp;
	int fd0, fd1;
	struct string_list args;
	char *pstr = "Package: ", *vstr = "Version: ", *pkg = NULL, *pv;
	struct stat st;
	size_t size;
	unsigned char bin_buffer[16];
	char *cwd, *rfile, *pfile;
	int err;
	struct string_list_el *i;

	string_list_init(&args);

	if (getcwd(path, sizeof(path)) == NULL)
		cwd = strdup(".");
	else
		cwd = strdup(path);
	if (cwd == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	/*
	see 2.2 from 
	http://www.debian.org/doc/manuals/apt-howto/ch-basico.en.html
	but without dpkg-scanpackages - it is perl script
	*/
	/* create temporary local cache */
	snprintf(path, sizeof(path), "%s/dists/" VZLREPO "/main/binary-%s", \
		pm->tmpdir, pm->pkgarch);
	if ((create_dir(path))) {
		vztt_logger(0, errno, "Can not create directory %s", path);
		return VZT_CANT_CREATE;
	}
	strncat(path, "/Packages", sizeof(path)-strlen(path)-1);
	if ((fp = fopen(path, "w")) == NULL) {
		vztt_logger(0, errno, "fopen(%s) failed", path);
		return VZT_CANT_OPEN;
	}
	rc = 0;

	string_list_for_each(packages, i) {
		/* convert deb to vz.deb */
		strncpy(path, i->s, sizeof(path));
		name = basename(path);
		if ((vzname = (char *)malloc(strlen(name) + 1 + 3)) == NULL) {
			vztt_logger(0, errno, "Cannot alloc memory");
			rc = VZT_CANT_ALLOC_MEM;
			break;
		}
		strcpy(vzname, name);

		strcpy(path, i->s);

		/* calculate md5sum */
		if (stat(path, &st) == -1) {
			vztt_logger(0, errno, "stat(%s) failed", path);
			rc = VZT_CANT_LSTAT;
			break;
		}
		if ((fc = fopen(path, "rb")) == NULL) {
			vztt_logger(0, errno, "fopen(%s) failed", path);
			rc = VZT_CANT_OPEN;
			break;
		}
		if(MD5Stream(fc, bin_buffer)) {
			vztt_logger(0, errno, "Can not calculate md5sum for %s", path);
			fclose(fc);
			rc = VZT_CANT_CALC_MD5SUM;
			break;
		}
		fclose(fc);

		/*
		 file protocol does not copy source packages into repository,
		 but use this packages directly.
		 Therefore we will copy vz.deb to repository
		*/
		snprintf(buf, sizeof(buf), "%s/dists/" VZLREPO "/%s", 
			pm->tmpdir, vzname);
		if ((rc = copy_file(buf, path))) {
			vztt_logger(0, 0, "Cannot copy %s to %s", path, buf);
			break;
		}

		/* create package records in repo metadata */
		if ((dir_fd=open("/", O_RDONLY)) < 0) {
			vztt_logger(0, errno, "Can not open / directory");
			rc = VZT_CANT_OPEN;
			break;
		}

		/* Open /dev/null on HN, because it's absent in environments */
		fd0 = open("/dev/null", O_WRONLY);
		fd1 = open("/dev/null", O_WRONLY);

		/* Next we chroot() to the target directory */
		if (chroot(pm->envdir) < 0) {
			vztt_logger(0, errno, "chroot(%s) failed", pm->envdir);
			rc = VZT_CANT_CHROOT;
			close(fd0);
			close(fd1);
			break;
		}
		if (fchdir(dir_fd) < 0) {
			vztt_logger(0, errno, "fchdir(%s) failed", pm->envdir);
			rc = VZT_CANT_CHDIR;
			close(fd0);
			close(fd1);
			break;
		}
		close(dir_fd);

		/* save stderr */
		dup2(STDERR_FILENO, fd1);
		/* redirect stderr to /dev/null */ 
		dup2(fd0, STDERR_FILENO);
		snprintf(cmd, sizeof(cmd), DPKG_DEB_BIN \
			" --info %s/dists/" VZLREPO "/%s control", pm->tmpdir, vzname);
		vztt_logger(2, 0, "%s", cmd);
		fc = popen(cmd, "r");
		err = errno;
		/* restore stderr */
		dup2(fd1, STDERR_FILENO);
		close(fd0);
		close(fd1);
		if (chroot(".") == -1) {
			vztt_logger(0, err, "Error in chroot(.)");
			rc = VZT_CANT_CHROOT;
			break;
		}
		if (fc == NULL) {
			vztt_logger(0, err, "Error in popen(%s)", cmd);
			rc = VZT_CANT_EXEC;
			break;
		}
		pv = NULL;
		while (fgets(buf, sizeof(buf), fc)) {
			fputs(buf, fp);
			/* also intercept Package: & Version: strings */
			for (ptr = buf + strlen(buf) - 1; *ptr == '\n' && ptr >= buf; ptr--)
				*ptr = '\0';
			if (strncmp(buf, pstr, strlen(pstr)) == 0) {
				pkg = strdup(buf + strlen(pstr));
			} else if (strncmp(buf, vstr, strlen(vstr)) == 0) {
				if (pkg == NULL)
					continue;
				for (ptr = buf + strlen(vstr); \
					*ptr && isspace(*ptr); ptr++) ;
				if (!*ptr) continue;
				size = strlen(pkg) + strlen(ptr) + 2;
				pv = (char *)malloc(size);
				snprintf(pv, size, "%s=%s", pkg, ptr);
				free((void *)pkg);
			}
		}
		if (chdir(cwd) == -1)
			vztt_logger(0, err, "Error in chdir(%s)", cwd);
		rc = pclose(fc);
		if (WEXITSTATUS(rc)) {
			vztt_logger(0, errno, "%s exit with retcode %d", \
				cmd, WEXITSTATUS(rc));
			rc = VZT_CANT_EXEC;
			break;
		}
		fprintf(fp, "Filename: dists/" VZLREPO "/%s\n", vzname);
		fprintf(fp, "Size: %lu\n", st.st_size);
		fprintf(fp, "MD5sum: ");
		for(j = 0; j < sizeof(bin_buffer); ++j)
			fprintf(fp, "%02x", bin_buffer[j]);
		fprintf(fp, "\n\n");

		free((void *)vzname);
		if (pv) {
			string_list_add(&args, pv);
			free((void *)pv);
		}
	}
	fclose(fp);
	if (rc)
		return rc;

	/* create temporary apt repository */
	snprintf(path, sizeof(path), \
		"%s/dists/" VZLREPO "/main/binary-%s/Release", \
		pm->tmpdir, pm->pkgarch);

	if ((fp = fopen(path, "w")) == NULL) {
		vztt_logger(0, errno, "fopen(%s) failed", path);
		return VZT_CANT_OPEN;
	}

	fputs("Archive: custom\nVersion: 1\nComponent: contrib\n"\
		"Origin: Custom\nLabel: Custom\n", fp);
	fprintf(fp, "Architecture: %s\n" , pm->pkgarch);
	fclose(fp);

	/* copy temporary repo metadata to local cache 
	to avoid 'apt-get update' evident call */
	strcpy(path, pm->tmpdir);
	for (ptr = path; *ptr; ptr++)
		if (*ptr == '/')
			*ptr = '_';
	snprintf(buf, sizeof(buf), \
		"%s/%s/lists/%s_dists_" VZLREPO "_main_binary-%s_Packages", \
		pm->basedir, pm->datadir, path, pm->pkgarch);
	if ((pfile = strdup(buf)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	snprintf(buf, sizeof(buf), \
		"%s/%s/lists/%s_dists_" VZLREPO "_main_binary-%s_Release", \
		pm->basedir, pm->datadir, path, pm->pkgarch);
	if ((rfile = strdup(buf)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	snprintf(path, sizeof(path), 
		"%s/dists/" VZLREPO "/main/binary-%s/Packages",
		pm->tmpdir, pm->pkgarch);
	copy_file(pfile, path);

	snprintf(path, sizeof(path), 
		"%s/dists/" VZLREPO "/main/binary-%s/Release",
 		pm->tmpdir, pm->pkgarch);
	copy_file(rfile, path);

	snprintf(cmd, sizeof(cmd), 
		"gzip %s/dists/" VZLREPO "/main/binary-%s/Packages", \
		pm->tmpdir, pm->pkgarch);
	if (system(cmd) == -1)
	{
		vztt_logger(0, errno, "system(%s) failed", cmd);
		return VZT_CANT_EXEC;
	}

	/* add record in repositories */
	snprintf(path, sizeof(path), "file:%s " VZLREPO " main", pm->tmpdir);
	if ((rc = repo_list_add(&pm->repositories, path, VZLREPO, 0)))
		return rc;

	rc = pm_modify(pm, command, &args, added, removed);
	unlink(rfile);
	unlink(pfile);
	string_list_clean(&args);

	return rc;
}

/* add common dpkg options for caching */
static int apt_cache_common_opts(
	struct Transaction *pm,
	struct string_list *opts)
{
	char buf[PATH_MAX+1];

	snprintf(buf, sizeof(buf), "--root=%s", pm->rootdir);
	string_list_add(opts, buf);
	snprintf(buf, sizeof(buf), "--vz_template=%s", pm->tmpldir);
	string_list_add(opts, buf);
	if (pm->force_openat || pm->vzfs_technologies) {
		snprintf(buf, sizeof(buf), "--osbasedir=%s", \
			pm->basesubdir);
		string_list_add(opts, buf);
	}
	if (pm->force_openat) {
		snprintf(buf, sizeof(buf), "--openat-force");
		string_list_add(opts, buf);
	} else if (pm->vzfs_technologies) {
		snprintf(buf, sizeof(buf), "--vz_technologies=%lu", \
			pm->vzfs_technologies);
		string_list_add(opts, buf);
	}
	return 0;
}

/* create initial OS template cache - install /sbin/init and libs */
int apt_create_init_cache(
	struct Transaction *pm,
	struct string_list *packages0,
	struct string_list *packages1,
	struct string_list *packages,
	struct package_list *installed)
{
	int rc = 0;
	char buf[PATH_MAX+1];
	struct package_list empty;
	struct package_list_el *p;
	struct string_list ls0;
	struct string_list ls;
	struct string_list_el *l;
	char *str, *tok, *delim=" 	";
	struct package *pkg;
	struct string_list opts;
	struct AptTransaction *apt= (struct AptTransaction *)pm;
	char *saveptr;

	package_list_init(&empty);
	string_list_init(&ls0);
	string_list_init(&ls);
	string_list_init(&opts);

	progress(PROGRESS_PKGMAN_DOWNLOAD_PACKAGES, 0, pm->progress_fd);

	/* download all at the first */
	for (l = packages0->tqh_first; l != NULL; l = l->e.tqe_next) {
		strncpy(buf, l->s, sizeof(buf));
		for (str = buf; ;str = NULL) {
			tok = strtok_r(str, delim, &saveptr);
			if (tok == NULL)
				break;
			string_list_add(&ls0, tok);
		}
	}
	for (l = ls0.tqh_first; l != NULL; l = l->e.tqe_next)
		string_list_add(&ls, l->s);
	for (l = packages1->tqh_first; l != NULL; l = l->e.tqe_next)
		string_list_add(&ls, l->s);
	for (l = packages->tqh_first; l != NULL; l = l->e.tqe_next)
		string_list_add(&ls, l->s);


	if ((rc = pm_create_outfile(pm)))
		return rc;
	string_list_add(&apt->dpkg_options, "--force-overwrite");
	string_list_add(&apt->dpkg_options, "--force-downgrade");
	apt->download_only = 1;
	if ((rc = apt_run(apt, APT_GET_BIN, "install", &ls)))
		return rc;
	apt->download_only = 0;
	string_list_clean(&pm->options);
	string_list_clean(&apt->dpkg_options);

	/* parse outfile */
	if ((rc = read_outfile(pm->outfile, installed, &empty)))
		return rc;

	pm_remove_outfile(pm);

	progress(PROGRESS_PKGMAN_DOWNLOAD_PACKAGES, 100, pm->progress_fd);

	progress(PROGRESS_PKGMAN_INST_PACKAGES1, 0, pm->progress_fd);

	for (l = ls0.tqh_first; l != NULL; l = l->e.tqe_next) {
		string_list_clean(&opts);
		string_list_add(&opts, "--no-scripts");
		apt_cache_common_opts(pm, &opts);
		string_list_add(&opts, "--force-all");
		string_list_add(&opts, "--unpack");
		pkg = NULL;
		for (p = installed->tqh_first; p != NULL; p = p->e.tqe_next) {
			pkg = NULL;
			if ((apt_pkg_cmp(l->s, p->p)) == 0) {
				pkg = p->p;
				break;
			}
		}
		if (pkg == NULL) {
			vztt_logger(0, 0, "package not found for %s", l->s);
			continue;
		}
		snprintf(buf, sizeof(buf), "%s/%s/%s/archives/", \
			pm->tmpldir, pm->basesubdir, pm->datadir);
		apt_get_pkg_dirname(pkg, buf+strlen(buf), sizeof(buf)-strlen(buf));
		strncat(buf, DEB_EXT, sizeof(buf)-strlen(buf)-1);
		string_list_add(&opts, buf);
		if ((rc = dpkg_run(apt, DPKG_BIN, &opts)))
			goto cleanup;
	}
	progress(PROGRESS_PKGMAN_INST_PACKAGES1, 100, pm->progress_fd);
	progress(PROGRESS_PKGMAN_INST_PACKAGES2, 0, pm->progress_fd);
	for (l = packages1->tqh_first; l != NULL; l = l->e.tqe_next) {
		string_list_clean(&opts);
		string_list_add(&opts, "--no-scripts");
		apt_cache_common_opts(pm, &opts);
		string_list_add(&opts, "--force-all");
		string_list_add(&opts, "--unpack");
		pkg = NULL;
		for (p = installed->tqh_first; p != NULL; p = p->e.tqe_next) {
			pkg = NULL;
			if ((apt_pkg_cmp(l->s, p->p)) == 0) {
				pkg = p->p;
				break;
			}
		}
		if (pkg == NULL) {
			vztt_logger(0, 0, "package not found for %s", l->s);
			continue;
		}
		snprintf(buf, sizeof(buf), "%s/%s/%s/archives/", \
			pm->tmpldir, pm->basesubdir, pm->datadir);
		apt_get_pkg_dirname(pkg, buf+strlen(buf), sizeof(buf)-strlen(buf));

		strncat(buf, DEB_EXT, sizeof(buf)-strlen(buf)-1);
		string_list_add(&opts, buf);
		if ((rc = dpkg_run(apt, DPKG_BIN, &opts)))
			goto cleanup;
	}

	progress(PROGRESS_PKGMAN_INST_PACKAGES2, 100, pm->progress_fd);

cleanup:
	string_list_clean(&opts);
	string_list_clean(&ls0);
	string_list_clean(&ls);
	package_list_clean_all(&empty);

	return rc;
}

/* create OS template cache - install post-init OS template packages */
int apt_create_post_init_cache(
	struct Transaction *pm,
	struct string_list *packages0,
	struct string_list *packages1,
	struct string_list *packages,
	struct package_list *installed)
{
	int rc = 0;
	char buf[PATH_MAX+1];
	char path[PATH_MAX+1];
	char cmd[2*PATH_MAX+1];
	struct package_list_el *p;
	struct string_list_el *l;
	char *str, *tok, *delim=" 	";
	struct package *pkg;
	struct string_list opts;
	struct AptTransaction *apt= (struct AptTransaction *)pm;
	char *saveptr;
	struct package_list added;
	struct package_list removed;

	progress(PROGRESS_PKGMAN_INST_PACKAGES3, 0, pm->progress_fd);

	string_list_init(&opts);
	package_list_init(&added);
	package_list_init(&removed);

	/* clear dpkg database */
	FILE *fp;
	snprintf(path, sizeof(path), "%s/var/lib/dpkg/status", pm->rootdir);
	if ((fp = fopen(path, "w")) == NULL) {
		vztt_logger(0, errno, "Can not open %s", path);
		return VZT_CANT_OPEN;
	}
	fputs("Package: dpkg\nVersion: 1.9.1\nStatus: install ok installed\n" \
		"Maintainer: Virtuozzo <wolf@virtuozzo.com>\n" \
		"Architecture: all\n" \
		"Description: dpkg patched by Virtuozzo\n", fp);
	fclose(fp);

	for (l = packages0->tqh_first; l != NULL; l = l->e.tqe_next) {
		string_list_clean(&opts);
		string_list_add(&opts, "--force-depends");
		string_list_add(&opts, "--force-confdef");
		snprintf(buf, sizeof(buf), "--veid=%s", pm->ctid);
		string_list_add(&opts, buf);
		snprintf(buf, sizeof(buf), "--envdir=%s", pm->envdir);
		string_list_add(&opts, buf);
		apt_cache_common_opts(pm, &opts);
		string_list_add(&opts, "--install");
		strncpy(buf, l->s, sizeof(buf));
		for (str = buf; ;str = NULL) {
			tok = strtok_r(str, delim, &saveptr);
			if (tok == NULL)
				break;
			pkg = NULL;
			for (p = installed->tqh_first; p != NULL; \
					p = p->e.tqe_next) {
				if ((apt_pkg_cmp(tok, p->p)) == 0) {
					pkg = p->p;
					break;
				}
			}
			if (pkg == NULL) {
				vztt_logger(0, 0, "package not found for %s", tok);
				continue;
			}
			snprintf(path, sizeof(path), "%s/%s/%s/archives/", \
				pm->tmpldir, pm->basesubdir, pm->datadir);
			apt_get_pkg_dirname(pkg, path+strlen(path), \
				sizeof(path)-strlen(path));
			strncat(path, DEB_EXT, sizeof(path)-strlen(path)-1);
			string_list_add(&opts, path);
		}
		if ((rc = dpkg_run(apt, DPKG_BIN, &opts)))
			goto cleanup;
	}

	string_list_clean(&opts);
	string_list_add(&opts, "--force-depends");
	string_list_add(&opts, "--force-confdef");
	string_list_add(&opts, "--force-overwrite");
	snprintf(buf, sizeof(buf), "--veid=%s", pm->ctid);
	string_list_add(&opts, buf);
	snprintf(buf, sizeof(buf), "--envdir=%s", pm->envdir);
	string_list_add(&opts, buf);
	apt_cache_common_opts(pm, &opts);
	string_list_add(&opts, "--unpack");
	for (l = packages1->tqh_first; l != NULL; l = l->e.tqe_next) {
		pkg = NULL;
		for (p = installed->tqh_first; p != NULL; p = p->e.tqe_next) {
			pkg = NULL;
			if ((apt_pkg_cmp(l->s, p->p)) == 0) {
				pkg = p->p;
				break;
			}
		}
		if (pkg == NULL) {
			vztt_logger(0, 0, "package not found for %s", l->s);
			continue;
		}
		snprintf(buf, sizeof(buf), "%s/%s/%s/archives/", \
			pm->tmpldir, pm->basesubdir, pm->datadir);
		apt_get_pkg_dirname(pkg, buf+strlen(buf), \
				sizeof(buf)-strlen(buf));

		strncat(buf, DEB_EXT, sizeof(buf)-strlen(buf)-1);
		string_list_add(&opts, buf);
	}

	if (string_list_size(packages1)) {
		if ((rc = dpkg_run(apt, DPKG_BIN, &opts)))
			goto cleanup;

		snprintf(cmd, sizeof(cmd), \
			VZCTL " exec2 %s \"LANG=C DEBIAN_FRONTEND=noninteractive "\
			"dpkg --configure --pending --force-configure-any "\
			"--force-confold --force-depends\"", pm->ctid);
		if ((rc = exec_cmd(cmd, pm->quiet)))
			goto cleanup;
	}

	if ((rc = pm_create_outfile(pm)))
		goto cleanup;

	/* resolve unmet dependencies : "apt-get -f install" 
	(Fix-Broken=true in config) */
	if ((rc = apt_action(pm, VZPKG_INSTALL, 0)))
		goto cleanup;
	if ((rc = apt_action(pm, VZPKG_INSTALL, packages)))
		goto cleanup;

	/* parse outfile */
	if ((rc = read_outfile(pm->outfile, &added, &removed)))
		goto cleanup;

	rc = merge_pkg_lists(&added, &removed, installed);

cleanup:
	pm_remove_outfile(pm);
	string_list_clean(&opts);
	package_list_clean(&added);
	package_list_clean(&removed);

	progress(PROGRESS_PKGMAN_INST_PACKAGES3, 100, pm->progress_fd);

	return rc;
}


/* create OS template cache - install OS template packages */
int apt_create_cache(
	struct Transaction *pm,
	struct string_list *packages0,
	struct string_list *packages1,
	struct string_list *packages,
	struct package_list *installed)
{
	int rc;

	if ((rc = apt_create_init_cache(pm, packages0, packages1, packages, installed)) != 0)
		return rc;

	if ((rc = apt_create_post_init_cache(pm, packages0, packages1, packages, installed)) != 0)
		return rc;

	return 0;
}

/*
 clean local apt cache and remove all non-directories from: 
 <basedir>/pm/{,list,lists,archives}
*/
int apt_clean_local_cache(struct Transaction *pm)
{
	int rc;
	char path[PATH_MAX+1];

	snprintf(path, sizeof(path), "%s/%s", pm->basedir, pm->datadir);
	if (access(path, F_OK))
		return 0;

	if ((rc = apt_action(pm, VZPKG_CLEAN, NULL)))
		return rc;

	snprintf(path, sizeof(path), "%s/%s/" PM_LIST_SUBDIR, 
			pm->basedir, pm->datadir);
	remove_files_from_dir(path);

	snprintf(path, sizeof(path), "%s/%s/lists", pm->basedir, pm->datadir);
	remove_files_from_dir(path);

	snprintf(path, sizeof(path), "%s/%s/lists/partial", pm->basedir, pm->datadir);
	remove_files_from_dir(path);

	snprintf(path, sizeof(path), "%s/%s/archives", pm->basedir, pm->datadir);
	remove_files_from_dir(path);

	snprintf(path, sizeof(path), "%s/%s", pm->basedir, pm->datadir);
	remove_files_from_dir(path);

	return 0;
}

/* Read debian package(s) info from <fp>, parse and
   put into struct pkg_info * list <ls> */ 
static int read_deb_info(FILE *fp, void *data)
{
	char buf[PATH_MAX+1];
	char *str;
	int is_descr = 0;
	struct pkg_info *p = NULL;
	struct string_list description;
	struct pkg_info_list *ls = (struct pkg_info_list *)data;

	string_list_init(&description);
	while(fgets(buf, sizeof(buf), fp)) {
		if (strncmp(buf, "Package:", strlen("Package:")) == 0) {
			is_descr = 0;
			str = cut_off_string(buf + strlen("Package:"));
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

		if (strncmp(buf, "Architecture:", \
				strlen("Architecture:")) == 0) {
			str = cut_off_string(buf + strlen("Architecture:"));
			if (str)
				p->arch = strdup(str);
		}
		else if (strncmp(buf, "Version:", \
				strlen("Version:")) == 0) {
			str = cut_off_string(buf + strlen("Version:"));
			if (str)
				p->version = strdup(str);
		}
		else if (strncmp(buf, "Description:", \
				strlen("Description:")) == 0) {
			str = cut_off_string(buf + strlen("Description:"));
			if (str == NULL)
				continue;
			p->summary = strdup(str);
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
	if (p != NULL) {
		if (p->description == NULL)
			string_list_to_array(&description, &p->description);
	}
	string_list_clean(&description);

	return 0;
}

/* get package info */
int apt_get_info(
		struct Transaction *pm, 
		const char *package, 
		struct pkg_info_list *ls)
{
	int rc;
	struct string_list args;
	struct string_list envs;
	struct AptTransaction *apt = (struct AptTransaction *)pm;
	int d = pm->debug;

	string_list_init(&args);
	string_list_init(&envs);

	pm->debug = 0;
	if ((rc = apt_create_config(apt)))
		return rc;
	pm->debug = d;

	/* apt parameters */
	string_list_add(&args, "-c");
	string_list_add(&args, apt->apt_conf);
	string_list_add(&args, "show");
	string_list_add(&args, (char *)package);

	/* add proxy in environments */
	if ((rc = add_proxy_env(&apt->http_proxy, HTTP_PROXY, &envs)))
		return rc;
	if ((rc = add_proxy_env(&apt->ftp_proxy, FTP_PROXY, &envs)))
		return rc;
	if ((rc = add_proxy_env(&apt->https_proxy, HTTPS_PROXY, &envs)))
		return rc;

	/* add templates environments too */
	if ((rc = add_tmpl_envs(pm->tdata, &envs)))
		return rc;

	/* run cmd from chroot environment */
	if ((rc = run_from_chroot2(APT_CACHE_BIN, pm->envdir, pm->debug,
		pm->ign_pm_err, &args, &envs, pm->osrelease,
		read_deb_info, (void *)ls)))
		return rc;

	apt_remove_config(apt);

	/* free mem */
	string_list_clean(&args);
	string_list_clean(&envs);

	return rc;
}

/* The same as read_deb_info, skip non-istalled packages only */
static int read_inst_deb_info(FILE *fp, void *data)
{
	char buf[PATH_MAX+1];
	char *str;
	int is_descr = 0;
	int installed = 0;
	struct pkg_info *p = NULL;
	struct string_list description;
	struct pkg_info_list *ls = (struct pkg_info_list *)data;

	string_list_init(&description);
	while(fgets(buf, sizeof(buf), fp)) {
		if (strncmp(buf, "Status:", \
				strlen("Status:")) == 0) {
			if (strncmp(buf, "Status:install ", 
					strlen("Status:install ")))
				installed = 0;
			else
				installed = 1;
			continue;
		}
		if (!installed)
			continue;
		if (strncmp(buf, "Package:", strlen("Package:")) == 0) {
			is_descr = 0;
			str = cut_off_string(buf + strlen("Package:"));
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

		if (strncmp(buf, "Architecture:", \
				strlen("Architecture:")) == 0) {
			str = cut_off_string(buf + strlen("Architecture:"));
			if (str)
				p->arch = strdup(str);
		} else if (strncmp(buf, "Version:", \
				strlen("Version:")) == 0) {
			str = cut_off_string(buf + strlen("Version:"));
			if (str)
				p->version = strdup(str);
		} else if (strncmp(buf, "Description:", \
				strlen("Description:")) == 0) {
			str = cut_off_string(buf + strlen("Description:"));
			if (str == NULL)
				continue;
			p->summary = strdup(str);
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
	if (p != NULL) {
		if (p->description == NULL)
			string_list_to_array(&description, &p->description);
	}
	string_list_clean(&description);

	return 0;
}

/* get package info */
int deb_get_info(
		struct Transaction *apt, 
		const char *package, 
		struct pkg_info_list *ls)
{
	int rc = 0;
	struct string_list args;
	struct string_list envs;
	char buf[PATH_MAX+1];

	string_list_init(&args);
	string_list_init(&envs);

	/* dpkg parameters */
	string_list_add(&args, "--admindir");
	snprintf(buf, sizeof(buf), "%s/var/lib/dpkg", apt->rootdir);
	string_list_add(&args, buf);
	string_list_add(&args, "--showformat");
	string_list_add(&args, 
		"Status:${Status}\\n"\
		"Package:${Package}\\n"\
		"Version:${Version}\\n"\
		"Architecture:${Architecture}\\n"\
		"Description:${Description}\\n\\n");
	string_list_add(&args, "--show");
	string_list_add(&args, (char *)package);

	/* run cmd from chroot environment */
	rc = run_from_chroot2(DPKG_QUERY_BIN, apt->envdir, apt->debug, \
		apt->ign_pm_err, &args, &envs, apt->osrelease, \
		read_inst_deb_info, (void *)ls);

	/* free mem */
	string_list_clean(&args);
	string_list_clean(&envs);

	if (rc == 0 && pkg_info_list_empty(ls)) {
		/* dpkg-query return 0 for available packages */
		vztt_logger(0, 0, "Packages %s does not installed", package);
		rc = VZT_PM_FAILED;
	}
	return rc;
}

/* remove per-repositories template cache directories */
int apt_remove_local_caches(struct Transaction *pm, char *reponame)
{
	return 0;
}


/* last repair chance: try fetch package <pkg> from <repair_mirror>
   Load <repair_mirror>/osname osver main other
   <repair_mirror>/osname/dists/osver/{main,other}
   <repair_mirror>/osname/pool/{main,other}
 */
int apt_last_repair_fetch(
		struct Transaction *pm,
		struct package *pkg,
		const char *repair_mirror)
{
	int rc = 0;
	char *mirror;
	size_t size;
	char buf[NAME_MAX+1];
	char *categories = "main other";
	struct string_list args;
	struct AptTransaction *apt = (struct AptTransaction *)pm;
	int data_source;

	string_list_init(&args);

	/* clean repositories&mirrorlists */
	repo_list_clean(&pm->repositories);
	repo_list_clean(&pm->mirrorlists);
	/* create mirror url */
	size = strlen(repair_mirror) + 1 + 
		strlen(pm->tdata->base->osname) + 1 +
		strlen(pm->tdata->base->osver) + 1 +
		strlen(categories) + 1;
	if ((mirror = (char *)malloc(size)) == NULL)
		return vztt_error(VZT_SYSTEM, errno, "malloc()");
	snprintf(mirror, size, "%s/%s %s %s", repair_mirror, 
		pm->tdata->base->osname, pm->tdata->base->osver, categories);
	if ((rc = repo_list_add(&pm->repositories, mirror, "repair_mirror", 0)))
		goto cleanup;

	if ((rc = apt_get_int_pkgname(pkg, buf, sizeof(buf))))
		goto cleanup;
	if ((rc = string_list_add(&args, buf)))
		goto cleanup;


	/* update metadata */
	data_source = pm->data_source;
	pm->data_source = OPT_DATASOURCE_REMOTE;
	string_list_add(&pm->options, "APT::Get::List-Cleanup=false");
	rc = apt_run(apt, APT_GET_BIN, "update", NULL);
	string_list_clean(&pm->options);
	pm->data_source = data_source;
	if (rc)
		goto cleanup;

	rc = apt_action(pm, VZPKG_GET, &args);
	string_list_clean(&args);

cleanup:
	free(mirror);
	return 0;
}

/* find (struct package *) in list<struct package *> 
 for name, [epoch], version, release and arch (if arch is defined) 
 Note: this code suppose that pkg->evr is _real_ package evr */
struct package_list_el * apt_package_find_nevra(
		struct package_list *packages,
		struct package *pkg)
{
	struct package_list_el *i;

	/* in vz3.0 vzpackages list sometimes does not 
	   content epoch in evr (#99696) */
	char *evr = strchr(pkg->evr, ':');

	package_list_for_each(packages, i) {
		/* compare name & arch */
		if (cmp_pkg(i->p, pkg))
			continue;

		if (strcmp(pkg->evr, i->p->evr) == 0)
			return i;

		if (evr) {
			/* try to compare without epoch */
			if (strcmp(evr+1, i->p->evr) == 0)
				return i;
		}
	}
	return NULL;
}

int apt_clone_metadata(
		struct Transaction *pm, 
		char *sname, 
		char *dname)
{
	return 0;
}

int apt_clean_metadata_symlinks(
		struct Transaction *pm, 
		char *name)
{
	return 0;
}

/* parse template area directory name (name_[epoch%3a]version-release_arch) 
and create struct package */
int apt_parse_vzdir_name(char *dirname, struct package **pkg)
{
	char buf[PATH_MAX+1];
	char *p, *name, *evr, *arch;

	*pkg = NULL;
	strncpy(buf, dirname, sizeof(buf));
	/* get name */
	name = buf;

	if ((p = strchr(name, '_')) == NULL)
		return vztt_error(VZT_CANT_PARSE, 0, 
			"Can't find EVR : %s", dirname);
	*p = '\0';
	evr = ++p;
	if ((p = strchr(evr, '%'))) {
		/* replace %3a on : in evr */
		if (strncmp(p, "%3a", 3) == 0) {
			*(p++) = ':';
			memmove(p, p+2, strlen(p)-1);
		}
	}
	if ((p = strchr(evr, '_')) == NULL)
		return vztt_error(VZT_CANT_PARSE, 0, 
			"Can't find arch : %s", dirname);
	*p = '\0';
	arch = ++p;

	/* create new */
	if ((*pkg = create_structp(name, arch, evr, NULL)) == NULL) {
		vztt_logger(0, errno, "Can't alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	return 0;
}
