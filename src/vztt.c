/*
 * Copyright (c) 2015-2017, Parallels International GmbH
 * Copyright (c) 2017-2019 Virtuozzo International GmbH. All rights reserved.
 *
 * This file is part of OpenVZ. OpenVZ is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Our contact details: Virtuozzo International GmbH, Vordergasse 59, 8200
 * Schaffhausen, Switzerland.
 *
 * main vzpkg module
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
#include <time.h>
#include <locale.h>
#include <vzctl/libvzctl.h>

#include "util.h"
#include "vztt.h"
#include "appcache.h"
#include "list_avail.h"

#define OPT_OBJECTS_NONE -1

typedef enum vztt_cmd_t {
	VZTT_CMD_NONE = 0,
	VZTT_CMD_LIST,
	VZTT_CMD_REPAIR,
	VZTT_CMD_UPGRADE,
	VZTT_CMD_CLEAN,
	VZTT_CMD_STATUS,
	VZTT_CMD_UPDATE_METADATA,
	VZTT_CMD_INSTALL,
	VZTT_CMD_UPDATE,
	VZTT_CMD_REMOVE,
	VZTT_CMD_LOCALINSTALL,
	VZTT_CMD_LOCALUPDATE,
	VZTT_CMD_CREATE_CACHE,
	VZTT_CMD_UPDATE_CACHE,
	VZTT_CMD_REMOVE_CACHE,
	VZTT_CMD_LINK,
	VZTT_CMD_FETCH,
	VZTT_CMD_INFO,
	VZTT_CMD_INSTALL_TEMPLATE,
	VZTT_CMD_UPDATE_TEMPLATE,
	VZTT_CMD_REMOVE_TEMPLATE,
	VZTT_CMD_UPGRADE_AREA,
	VZTT_CMD_GET_AREA_VZFS,
	VZTT_CMD_GET_BACKUP_APPS,
	VZTT_CMD_SYNC_VZPACKAGES,
	VZTT_CMD_HELP,
	VZTT_CMD_CREATE_APPCACHE,
	VZTT_CMD_UPDATE_APPCACHE,
	VZTT_CMD_REMOVE_APPCACHE,
	VZTT_CMD_LIST_APPCACHE,
	VZTT_CMD_CREATE_PLOOP_IMAGE,
} vztt_cmd_t;

enum {
	PARAM_FORCE         = 'f',
	PARAM_DEBUG         = 'd',
	PARAM_TEST          = 'n',
	PARAM_QUIET         = 'q',
	PARAM_CACHE         = 'C',
	PARAM_REMOTE        = 'r',
	PARAM_SKIPLOCK      = '2',
	PARAM_WITH_SUMMARY  = 'S',
	PARAM_CUSTOM_PKG    = 'u',
	PARAM_VZDIR         = '1',
	PARAM_PACKAGES      = 'p',
	PARAM_WITH_DEPENDS  = 'w',
	PARAM_OS_ONLY       = 'O',
	PARAM_APP_ONLY      = 'A',
	PARAM_CLEAN_PKGS    = 'k',
	PARAM_TMPL          = 't',
	PARAM_CLEAN_ALL     = 'a',
	PARAM_FOR_OBJ       = 'F',
	PARAM_STDI_TYPE     = '3',
	PARAM_STDI_VERSION  = '4',
	PARAM_STDI_TECH     = '5',
	PARAM_STDI_FORMAT   = '6',
	PARAM_ONLY_STD      = 'T',
	PARAM_WITH_STD      = 'W',
	PARAM_CACHED_ONLY   = 'c',
	PARAM_PKGID         = 'i',
	PARAM_FORCE_OPENAT  = 'o',
	PARAM_EXPANDED      = 'e',
	PARAM_INTERACTIVE   = 'I',
	PARAM_FORCE_SHARED  = 's',
	PARAM_SEPARATE      = 'P',
	PARAM_UPDATE_CACHE  = '7',
	PARAM_SKIP_DB       = '8',
	PARAM_FORCE_VZCTL   = '9',
	PARAM_GROUPS        = 'g',
	PARAM_NO_VZUP2DATE  = 'v',
	PARAM_RELEASE_VERSION = 'Q',
	PARAM_ALLOW_ERASING = 'D',
	PARAM_CONFIG        = 0,
	PARAM_APP_OSTEMPLATE = 1,
	PARAM_APP_APPTEMPLATE = 2,
	PARAM_AVAILABLE = 3,
	PARAM_VEFSTYPE = 4,
	PARAM_PROGRESS_FD = 5,
	PARAM_VEIMGFMT = 6,
	PARAM_TIMEOUT = 7,
};

/* global - use in vztt_logger */
int debug_level;

void usage(const char * progname, int rc)
{
	fprintf(stderr,PRODUCT_NAME_SHORT " EZ template management tool.\n");
	fprintf(stderr,"Usage:\n");
	fprintf(stderr,"%s install | update | remove | localinstall | localupdate | upgrade |\n", progname);
	fprintf(stderr,"    list | info | clean | fetch | status | link | update metadata |\n");
	fprintf(stderr,"    create cache | update cache | remove cache |\n");
	fprintf(stderr,"    create appcache | update appcache | list appcache | remove appcache |\n");
	fprintf(stderr,"    remove template | install template | update template | help\n");
	fprintf(stderr,"%s install [-p|-g] [-C|-r] [-n] [-f] [-q|-d <level>]\n", progname);
	fprintf(stderr,"           <VEID>|<VENAME> <object> [...]\n");
	fprintf(stderr,"%s update  [-p|-g|-t] [-C|-r] [-n] [-f] [-q|-d <level>]\n", progname);
	fprintf(stderr,"           <VEID>|<VENAME> [<object> [...]]\n");
	fprintf(stderr,"%s remove  [-p|-g] [-C|-r] [-n] [-f|-w] [-q|-d <level>]\n", progname);
	fprintf(stderr,"           <VEID>|<VENAME> <object> [...]\n");
	fprintf(stderr,"%s localinstall [-C|-r] [-n] [-q|-d <level>]\n", progname);
	fprintf(stderr,"           <VEID>|<VENAME> <file> [...]\n");
	fprintf(stderr,"%s localupdate  [-C|-r] [-n] [-q|-d <level>]\n", progname);
	fprintf(stderr,"           <VEID>|<VENAME> <file> [...]\n");
	fprintf(stderr,"%s upgrade      [-C|-r] [-n] [-q|-d <level>] <VEID>|<VENAME>\n", progname);
	fprintf(stderr,"%s list [-S] [-p|-g [-u [-C|-r]]] [-q|-d <level>] \n", progname);
	fprintf(stderr,"           [--available] <VEID>|<VENAME> [...]\n");
	fprintf(stderr,"%s list [-S] [-p|-g [-C|-r]] [-A|-O] [-q|-d <level>] \n", progname);
	fprintf(stderr,"           [--available] [<OS template> [...]]\n");
	fprintf(stderr,"%s create cache [-C|-r] [-f] [-q|-d <level>] [<OS template> [...]]\n", progname);
	fprintf(stderr,"%s update cache [-C|-r] [ --update-cache ] [<OS template> [...]]\n", progname);
	fprintf(stderr,"%s remove cache [-q|-d <level>] [<OS template> [...]]\n", progname);
	fprintf(stderr,"%s create appcache [-f] [-q|-d <level>] [--config"\
			" <config>] [ --ostemplate <ostemplate> ]"\
			" [ --apptemplate <apptemplate<,apptemplate...>> ]\n", progname);
	fprintf(stderr,"%s create image <OS template> <path>\n", progname);
	fprintf(stderr,"%s update appcache [-f] [-q|-d <level>] [--config"\
			" <config>] [ --ostemplate <ostemplate> ]"\
			" [ --apptemplate <apptemplate<,apptemplate...>> ]"\
			" [ --update-cache ]\n", progname);
	fprintf(stderr,"%s remove appcache [-f] [-q|-d <level>] [--config"\
			" <config>] [ --ostemplate <ostemplate> ]"\
			" [ --apptemplate <apptemplate<,apptemplate...>> ]\n", progname);
	fprintf(stderr,"%s list appcache\n", progname);
	fprintf(stderr,"%s info [-F <OS template>|<VEID>|<VENAME>] [-q|-d <level>] <template> \n", progname);
	fprintf(stderr,"           [name] [summary] [description] [packages] [repositories] [mirrorlist]\n");
	fprintf(stderr,"           [package_manager] [distribution] [technologies] [config_path]\n");
	fprintf(stderr,"           [version] [release] [arch] [package_manager_type]\n");
	fprintf(stderr,"%s info -p [-C|-r] [-F <OS template>|<VEID>|<VENAME>] [-q|-d <level>]\n", progname);
	fprintf(stderr,"           <package> [name] [version] [release] [arch] [summary] [description]\n");
	fprintf(stderr,"%s info -g [-C|-r] [-F <OS template>|<VEID>|<VENAME>] [-q|-d <level>] <group>\n", progname);
	fprintf(stderr,"%s install template [-f] [-q] [-F <OS template>] <template> ...\n", progname);
	fprintf(stderr,"%s update template  [-f] [-q] [-F <OS template>] <template> ...\n", progname);
	fprintf(stderr,"%s remove template  [-f] [-q] [-F <OS template>] <template> ...\n", progname);
	fprintf(stderr,"%s fetch [-C|-r] [-f] [-O|-A] [-P] [-q|-d <level>] "\
			"<OS template> [...]\n", progname);
	fprintf(stderr,"%s update metadata [-C|-r] [-q|-d <level>] [<OS template> [...]]\n", progname);
	fprintf(stderr,"%s status [-C|-r] [-q|-d <level>] <VEID>|<VENAME>|<OS template>\n", progname);
	fprintf(stderr,"%s clean [-k|-t|-a] [-f] [-n] [-q|-d <level>] [<OS template> [...]]\n", progname);
	fprintf(stderr,"%s link [-s] [-C|-r] [-q|-d <level>] <VEID>|<VENAME>\n", progname);
	fprintf(stderr,"%s help\n", progname);
/*	fprintf(stderr,"%s repair [options] <OS template> <vzpackages file>\n", progname);*/
/*	fprintf(stderr,"%s get_backup_apps [options] <veprivate> <veconfig>\n", progname);*/
	fprintf(stderr,"  Options:\n");
	fprintf(stderr,	"    -f/--force           force option\n");
	fprintf(stderr,	"    -w/--with-depends    remove also packages depending on that\n");
	fprintf(stderr,	"    -n/--check-only      check only mode\n");
	fprintf(stderr,	"    -q/--quiet           quiet mode\n");
	fprintf(stderr,	"    -d/--debug n         set debug level. Practical range : 0-5\n");
	fprintf(stderr,	"                         Default value is 0.\n");
	fprintf(stderr,	"    -p/--packages        interpret objects as packages name\n");
	fprintf(stderr,	"                         (instead of as templates by default)\n");
	fprintf(stderr,	"    -g/--groups          interpret objects as yum groups\n");
	fprintf(stderr,	"                         (only for rpm-based templates/containers)\n");
	fprintf(stderr,	"    -C/--cache           Seek entirely in local packages cache,\n");
	fprintf(stderr,	"                         don't get new packages from network.\n");
	fprintf(stderr,	"                         If something needed is not available locally -\n");
	fprintf(stderr,	"                         %s will fail.\n", progname);
	fprintf(stderr,	"    -u/--custom-pkg      report list of packages, installed into CT but\n");
	fprintf(stderr,	"                         are not available in the template repositories\n");
	fprintf(stderr,	"                         (for list command)\n");
	fprintf(stderr,"    -S/--with-summary    to print list of templates/packages with summary\n");
	fprintf(stderr,	"    -r/--remote          force to use remote metadata.\n");
	fprintf(stderr,	"    -O/--os              execute for OS templates only\n");
	fprintf(stderr,	"    -A/--app             execute for application templates only\n");
	fprintf(stderr,	"    -F/--for-os <ostemplate>/<VEID>/<VENAME>\n");
	fprintf(stderr,	"                         specify OS template or CT\n");
	fprintf(stderr,	"    -c/--cached          skip cacheable OS templates missed in the cache\n");
	fprintf(stderr,	"    -i/--pkgid           to print system-wide unique templated id\n");
	fprintf(stderr,	"                         instead of template name\n");
	fprintf(stderr,	"    -k/--clean-packages  clean local package cache\n");
	fprintf(stderr,	"    -t/--template        replaces --clean-template (deprecated)\n");
	fprintf(stderr,	"                         remove unused packages from the template area\n");
	fprintf(stderr,	"                         (for the clean command only)\n");
	fprintf(stderr,	"                         update all templates installed in the Container\n");
	fprintf(stderr,	"                         (for the update command only)\n");
	fprintf(stderr,	"    -a/--clean-all       clean both\n");
	fprintf(stderr,	"    -e/--expanded        use 'expanded' update mode:\n");
	fprintf(stderr,	"                         upgrade for yum and dist-upgrade for apt-get\n");
	fprintf(stderr,	"    -I/--interactive     use interactive mode of debian package management\n");
	fprintf(stderr,	"    -s/--force-shared    force action for template area on shared partition case\n");
	fprintf(stderr,	"    -P/--separate        execute transaction for each template separately\n");
	fprintf(stderr,	"    -v/--no-vzup2date    Don't call vzup2date to "\
				"download absent templates/environments.\n");
	fprintf(stderr,	"    --update-cache       update packages in "\
		"existing cache instead of cache recreation\n");
	fprintf(stderr,	"    --config <config>    Use given config for template app-caching\n");
	fprintf(stderr,	"    --ostemplate <ostemplate>\n"\
					"                         Redefine ostemplate in"\
			" given config for template app-caching.\n");
	fprintf(stderr,	"    --apptemplate <apptemplate<,apptemplate...>>\n"\
					"                         Redefine apptemplate in given config for template\n"\
					"                         app-caching.\n");
	fprintf(stderr,	"    --vefstype <VEFSTYPE>\n"\
					"                         Redefine the VEFSTYPE parameter in the vz global\n" \
					"                         configuration file\n");
	fprintf(stderr,	"    --veimgfmt <VEIMAGEFORMAT>\n"
					"                         Redefine the VEIMAGEFORMAT parameter in the vz global\n" \
					"                         configuration file\n");
	fprintf(stderr,	"    --timeout <seconds>\n"
					"                         Define the timeout interval for locked cache until it will be unlocked\n" \
					"                         Absent or zero value mean infinite period\n");
	fprintf(stderr,"    --releasever=<release_version> Add release version into yum cmd\n");
	fprintf(stderr,"    --allowerasing Add allowerasing argument into yum cmd\n");
/*	fprintf(stderr,"       --skip-db         do not check vzpackages in "\
		"internal packages database in repair mode\n");*/
/*	fprintf(stderr,"       --vzdir           report list of use by CT directories at template area\n");*/
/*	fprintf(stderr,"    -2/--skiplock        skip lock mode\n"); */
/*	fprintf(stderr,"    -o/--force-openat    force using openat syscall\n");*/

	exit(rc);
}

static int parse_cmd_line(
		int argc,
		char *argv[],
		struct options_vztt *opts_vztt)
{
	int c;
	char *p;
	struct option options[] =
	{
		{"force", no_argument, NULL, PARAM_FORCE},
		{"with-depends", no_argument, NULL, PARAM_WITH_DEPENDS},
		{"debug", required_argument, NULL, PARAM_DEBUG},
		{"check-only", no_argument, NULL, PARAM_TEST},
		{"quiet", no_argument, NULL, PARAM_QUIET},
		{"packages", no_argument, NULL, PARAM_PACKAGES},
		{"groups", no_argument, NULL, PARAM_GROUPS},
		{"cache", no_argument, NULL, PARAM_CACHE},
		{"remote", no_argument, NULL, PARAM_REMOTE},
		{"skiplock", no_argument, NULL, PARAM_SKIPLOCK},
		{"with-summary", no_argument, NULL, PARAM_WITH_SUMMARY},
		{"custom-pkg", no_argument, NULL, PARAM_CUSTOM_PKG},
		{"vzdir", no_argument, NULL, PARAM_VZDIR},
		{"os", no_argument, NULL, PARAM_OS_ONLY},
		{"app", no_argument, NULL, PARAM_APP_ONLY},
		{"clean-packages", no_argument, NULL, PARAM_CLEAN_PKGS},
		{"clean-template", no_argument, NULL, PARAM_TMPL},
		{"template", no_argument, NULL, PARAM_TMPL},
		{"clean-all", no_argument, NULL, PARAM_CLEAN_ALL},
		{"for-os", required_argument, NULL, PARAM_FOR_OBJ},
		{"old-info-type", no_argument, NULL, PARAM_STDI_TYPE},
		{"old-info-version", no_argument, NULL, PARAM_STDI_VERSION},
		{"old-info-technologies", no_argument, NULL, PARAM_STDI_TECH},
		{"old-format", no_argument, NULL, PARAM_STDI_FORMAT},
		{"old-template", no_argument, NULL, PARAM_ONLY_STD},
		{"with-old-template", no_argument, NULL, PARAM_WITH_STD},
		{"cached", no_argument, NULL, PARAM_CACHED_ONLY},
		{"pkgid", no_argument, NULL, PARAM_PKGID},
		{"force-openat", no_argument, NULL, PARAM_FORCE_OPENAT},
		{"expanded", no_argument, NULL, PARAM_EXPANDED},
		{"interactive", no_argument, NULL, PARAM_INTERACTIVE},
		{"force-shared", no_argument, NULL, PARAM_FORCE_SHARED},
		{"separate", no_argument, NULL, PARAM_SEPARATE},
		{"update-cache", no_argument, NULL, PARAM_UPDATE_CACHE},
		{"skip-db", no_argument, NULL, PARAM_SKIP_DB},
		{"force-vzctl", no_argument, NULL, PARAM_FORCE_VZCTL},
		{"no-vzup2date", no_argument, NULL, PARAM_NO_VZUP2DATE},
		{"config", required_argument, NULL, PARAM_CONFIG},
		{"ostemplate", required_argument, NULL, PARAM_APP_OSTEMPLATE},
		{"apptemplate", required_argument, NULL, PARAM_APP_APPTEMPLATE},
		{"available", no_argument, NULL, PARAM_AVAILABLE},
		{"vefstype", required_argument, NULL, PARAM_VEFSTYPE},
		{"progress", required_argument, NULL, PARAM_PROGRESS_FD},
		{"veimgfmt", required_argument, NULL, PARAM_VEIMGFMT},
		{"timeout", required_argument, NULL, PARAM_TIMEOUT},
		{"releasever", required_argument, NULL, PARAM_RELEASE_VERSION},
		{"allowerasing", no_argument, NULL, PARAM_ALLOW_ERASING},
		{ NULL, 0, NULL, 0 }
	};

	// Check for progress VZ_PROGRESS_FD
	p = getenv("VZ_PROGRESS_FD");
	if (p)
		opts_vztt->progress_fd = atoi(p);

	while (1)
	{
		c = getopt_long(argc, argv, "fd:nqCrSu12pF:Q:ciwAODTWoektaIsPvg0yYLZ", options, NULL);
		if (c == -1)
			break;
		switch (c)
		{
		case PARAM_FORCE:
			opts_vztt->flags |= OPT_VZTT_FORCE;
			break;
		case PARAM_WITH_DEPENDS:
			opts_vztt->flags |= OPT_VZTT_DEPENDS;
			break;
		case PARAM_TEST:
			opts_vztt->flags |= OPT_VZTT_TEST;
			break;
		case PARAM_QUIET:
			opts_vztt->flags |= OPT_VZTT_QUIET;
			break;
		case PARAM_CACHE:
			opts_vztt->data_source = OPT_DATASOURCE_LOCAL;
			break;
		case PARAM_REMOTE:
			opts_vztt->data_source = OPT_DATASOURCE_REMOTE;
			break;
		case PARAM_SKIPLOCK:
			opts_vztt->flags |= OPT_VZTT_SKIP_LOCK;
			break;
		case PARAM_WITH_SUMMARY:
			opts_vztt->fld_mask |= VZTT_INFO_SUMMARY;
			break;
		case PARAM_PACKAGES:
			opts_vztt->objects = OPT_OBJECTS_PACKAGES;
			break;
		case PARAM_GROUPS:
			opts_vztt->objects = OPT_OBJECTS_GROUPS;
			break;
		case PARAM_DEBUG:
			if (optarg == NULL) {
				vztt_logger(0, 0, "Bad debug level value");
				return VZT_BAD_PARAM;
			}
			if (strlen(optarg) == 0) {
				vztt_logger(0, 0, "Bad debug level value");
				return VZT_BAD_PARAM;
			}
			for(p=optarg; *p; p++) {
				if (!isdigit(*p)) {
					vztt_logger(0, 0, "Bad debug level value: %s", optarg);
					return VZT_BAD_PARAM;
				}
			}
			opts_vztt->debug = strtol(optarg, NULL, 10);
			break;
		case PARAM_CUSTOM_PKG :
			opts_vztt->flags |= OPT_VZTT_CUSTOM_PKG;
			break;
		case PARAM_VZDIR :
			opts_vztt->flags |= OPT_VZTT_VZ_DIR;
			opts_vztt->objects = OPT_OBJECTS_PACKAGES;
			break;
		case PARAM_OS_ONLY :
			opts_vztt->templates = OPT_TMPL_OS;
			break;
		case PARAM_APP_ONLY :
			opts_vztt->templates = OPT_TMPL_APP;
			break;
		case PARAM_CLEAN_PKGS :
			opts_vztt->clean = OPT_CLEAN_PKGS;
			break;
		case PARAM_TMPL :
			opts_vztt->clean = OPT_CLEAN_TMPL;
			/* For vzpkg update */
			opts_vztt->objects = OPT_OBJECTS_TEMPLATES;
			break;
		case PARAM_CLEAN_ALL :
			opts_vztt->clean = OPT_CLEAN_PKGS | OPT_CLEAN_TMPL;
			break;
		case PARAM_FOR_OBJ :
			if (optarg == NULL) {
				vztt_logger(0, 0, "Bad --for-os value");
				return VZT_BAD_PARAM;
			}
			if (*optarg == '.') optarg++;
			if ((opts_vztt->for_obj = strdup(optarg)) == NULL) {
				vztt_logger(0, errno, "Cannot alloc memory");
				return VZT_CANT_ALLOC_MEM;
			}
			break;
		case PARAM_STDI_TYPE :
			opts_vztt->flags |= OPT_VZTT_STDI_TYPE;
 			break;
		case PARAM_STDI_VERSION :
			opts_vztt->flags |= OPT_VZTT_STDI_VERSION;
			break;
		case PARAM_STDI_TECH :
			opts_vztt->flags |= OPT_VZTT_STDI_TECH;
			break;
		case PARAM_STDI_FORMAT :
			opts_vztt->flags |= OPT_VZTT_STDI_FORMAT;
			break;
		case PARAM_ONLY_STD:
			opts_vztt->flags |= OPT_VZTT_ONLY_STD;
			break;
		case PARAM_WITH_STD:
			opts_vztt->flags |= OPT_VZTT_WITH_STD;
			break;
		case PARAM_CACHED_ONLY:
			opts_vztt->flags |= OPT_VZTT_CACHED_ONLY;
			break;
		case PARAM_PKGID:
			opts_vztt->flags |= OPT_VZTT_PKGID;
			break;
		case PARAM_FORCE_OPENAT:
			opts_vztt->flags |= OPT_VZTT_FORCE_OPENAT;
			break;
		case PARAM_EXPANDED:
			opts_vztt->flags |= OPT_VZTT_EXPANDED;
			break;
		case PARAM_INTERACTIVE:
			opts_vztt->flags |= OPT_VZTT_INTERACTIVE;
			break;
		case PARAM_FORCE_SHARED:
			opts_vztt->flags |= OPT_VZTT_FORCE_SHARED;
			break;
		case PARAM_SEPARATE:
			opts_vztt->flags |= OPT_VZTT_SEPARATE;
			break;
		case PARAM_UPDATE_CACHE:
			opts_vztt->flags |= OPT_VZTT_UPDATE_CACHE;
			break;
		case PARAM_SKIP_DB:
			opts_vztt->flags |= OPT_VZTT_SKIP_DB;
			break;
		case PARAM_NO_VZUP2DATE:
			opts_vztt->flags &=~ OPT_VZTT_USE_VZUP2DATE;
			break;
		case PARAM_CONFIG:
			if (optarg == NULL || strlen(optarg) == 0) {
				vztt_logger(0, 0, "Bad config name");
				return VZT_BAD_PARAM;
			}
			opts_vztt->config = strdup(optarg);
			break;
		case PARAM_APP_OSTEMPLATE:
			if (optarg == NULL || strlen(optarg) == 0) {
				vztt_logger(0, 0, "Bad ostemplate name");
				return VZT_BAD_PARAM;
			}
			opts_vztt->app_ostemplate = strdup(optarg);
			break;
		case PARAM_APP_APPTEMPLATE:
			if (optarg == NULL || strlen(optarg) == 0) {
				vztt_logger(0, 0, "Bad apptemplate name");
				return VZT_BAD_PARAM;
			}
			opts_vztt->app_apptemplate = strdup(optarg);
			break;
		case PARAM_AVAILABLE:
			opts_vztt->flags |= OPT_VZTT_AVAILABLE;
			break;
		case PARAM_FORCE_VZCTL:
			opts_vztt->flags |= OPT_VZTT_FORCE_VZCTL;
			break;
		case PARAM_VEFSTYPE:
			if (optarg == NULL || strlen(optarg) == 0) {
				vztt_logger(0, 0, "parameter 'vefstype' is empty");
				return VZT_BAD_PARAM;
			}
			opts_vztt->vefstype = strdup(optarg);
			break;
		case PARAM_VEIMGFMT:
			if (optarg == NULL || strlen(optarg) == 0) {
				vztt_logger(0, 0, "Parameter 'veimgfmt' is empty");
				return VZT_BAD_PARAM;
			}
			opts_vztt->image_format = strdup(optarg);
			break;
		case PARAM_PROGRESS_FD:
			if (optarg == NULL || strlen(optarg) == 0 || atoi(optarg) == 0) {
				vztt_logger(0, 0, "Bad progress fd number");
				return VZT_BAD_PARAM;
			}
			opts_vztt->progress_fd = atoi(optarg);
			break;
		case PARAM_TIMEOUT:
			if (optarg == NULL || strlen(optarg) == 0) {
				vztt_logger(0, 0, "Parameter 'timeout' is empty");
				return VZT_BAD_PARAM;
			}
			opts_vztt->timeout = strtol(optarg, NULL, 10);
			break;
		case PARAM_RELEASE_VERSION :
			if (optarg == NULL || strlen(optarg) == 0) {
				vztt_logger(0, 0, "Bad release version value");
				return VZT_BAD_PARAM;
			}
			opts_vztt->release_version = strdup(optarg);
			break;
		case PARAM_ALLOW_ERASING:
			opts_vztt->flags |= OPT_VZTT_ALLOW_ERASING;
			break;
		default :
			return VZT_BAD_PARAM;
		}
	}
	if (opts_vztt->flags & OPT_VZTT_QUIET)
		opts_vztt->debug = 0;

	return 0;
}

/* fill string by space up to <pos> */
static void fill_string(char *buf, int size, int pos)
{
	int from = strlen(buf);
	int i;

	if (from >= pos)
		return;

	if (pos >= size)
		pos = size;

	for (i = from; i < pos; i++)
		buf[i] = ' ';
	buf[pos] = '\0';
}

#define TMPL_RECORD_MAX_SIZE 200
#define TMPL_RECORD_APP_POS 20
#define TMPL_RECORD_INFO_POS 34

/* print os template record for list command */
static void print_os_tmpl_record(
		struct tmpl_list_el *el,
		struct options_vztt *opts_vztt)
{
	char buf[TMPL_RECORD_MAX_SIZE+1];

	if (opts_vztt->flags & OPT_VZTT_CACHED_ONLY) {
		if (el->info->cached == NULL)
			return;
		if (strcmp(el->info->cached, "yes"))
			return;
	}

	buf[0] = '\0';
	if (opts_vztt->flags & OPT_VZTT_PKGID)
		strncat(buf, ".", sizeof(buf)-strlen(buf)-1);
	strncat(buf, el->info->name, sizeof(buf)-strlen(buf)-1);
	if (opts_vztt->flags & OPT_VZTT_STDI_FORMAT) {
		printf("%s\n", buf);
		return;
	}
	fill_string(buf, sizeof(buf), TMPL_RECORD_INFO_POS);
	strncat(buf, " ", sizeof(buf)-strlen(buf)-1);
	if (opts_vztt->fld_mask & VZTT_INFO_SUMMARY) {
		if (el->info->summary) {
			strncat(buf, ":", sizeof(buf)-strlen(buf)-1);
			strncat(buf, el->info->summary, sizeof(buf)-strlen(buf)-1);
		}
	} else {
		if (el->timestamp && !(opts_vztt->flags & OPT_VZTT_QUIET))
			strncat(buf, el->timestamp, sizeof(buf)-strlen(buf)-1);
	}
	printf("%s\n", buf);
}

/* print app template record for list command */
static void print_app_tmpl_record(
		char *ostemplate,
		struct tmpl_list_el *el,
		struct options_vztt *opts_vztt)
{
	char buf[TMPL_RECORD_MAX_SIZE+1];

	buf[0] = '\0';
	if (opts_vztt->flags & OPT_VZTT_PKGID)
		strncat(buf, ".", sizeof(buf)-strlen(buf)-1);
	if (opts_vztt->flags & OPT_VZTT_STDI_FORMAT) {
		strncat(buf, el->info->name, sizeof(buf)-strlen(buf)-1);
		printf("%s\n", buf);
		return;
	}
	strncat(buf, ostemplate, sizeof(buf)-strlen(buf)-1);
	fill_string(buf, sizeof(buf), TMPL_RECORD_APP_POS);
	strncat(buf, " ", sizeof(buf)-strlen(buf)-1);
	if (opts_vztt->flags & OPT_VZTT_PKGID)
		strncat(buf, ".", sizeof(buf)-strlen(buf)-1);
	strncat(buf, el->info->name, sizeof(buf)-strlen(buf)-1);
	fill_string(buf, sizeof(buf), TMPL_RECORD_INFO_POS);
	strncat(buf, " ", sizeof(buf)-strlen(buf)-1);
	if (opts_vztt->fld_mask & VZTT_INFO_SUMMARY) {
		if (el->info->summary) {
			strncat(buf, ":", sizeof(buf)-strlen(buf)-1);
			strncat(buf, el->info->summary, sizeof(buf)-strlen(buf)-1);
		}
	} else {
		if (el->timestamp && !(opts_vztt->flags & OPT_VZTT_QUIET))
			strncat(buf, el->timestamp, sizeof(buf)-strlen(buf)-1);
	}
	printf("%s\n", buf);
}

/* show templates for base os template */
static int show_templates_list(char *ostemplate,
		struct options_vztt *opts_vztt)
{
	int rc;
	struct tmpl_list_el **ls;
	size_t i;

	if ((rc = vztt2_get_templates_list(ostemplate, opts_vztt, &ls)))
		return rc;
	for (i = 0; ls[i]; i++) {
		if (ls[i]->is_os)
                {
                    if ( !(ls[i]->info->confdir &&
                            strcmp(ls[i]->info->confdir,FAKE_CONFDIR) == 0))
			print_os_tmpl_record(ls[i], opts_vztt);
                }
		else
			print_app_tmpl_record(ostemplate, ls[i], opts_vztt);
	}
	vztt_clean_templates_list(ls);
	return 0;
}

/* show templates for base os template */
static int show_available_templates_list(char **ostemplates,
		struct options_vztt *opts_vztt)
{
	int rc;
	struct tmpl_list_el **ls;
        char * ostemplate = NULL;
	size_t i;

	if ((rc = list_avail_get_full_list(ostemplates, &ls, OPT_TMPL_APP | OPT_TMPL_OS )))
		return rc;

	for (i = 0; ls[i]; i++) {
		if (ls[i]->is_os)
                {
                        ostemplate = ls[i]->info->name;
                        if( (opts_vztt->templates != OPT_TMPL_APP) && !
                                (ls[i]->info->confdir &&
                                (strcmp(ls[i]->info->confdir, FAKE_CONFDIR) == 0)) )
        			print_os_tmpl_record(ls[i], opts_vztt);
                } else if( opts_vztt->templates != OPT_TMPL_OS )
			print_app_tmpl_record(ostemplate, ls[i], opts_vztt);
	}
	vztt_clean_templates_list(ls);
	return 0;
}

/* show list of all templates */
int cmd_list_all_templates(char *arg0, struct options_vztt *opts_vztt)
{
	int rc = 0;
	size_t i;
	char **base_os;

	if ((opts_vztt->flags & OPT_VZTT_CUSTOM_PKG) ||
	    (opts_vztt->objects == OPT_OBJECTS_PACKAGES) ||
	    (opts_vztt->objects == OPT_OBJECTS_GROUPS))
			usage(arg0, VZT_BAD_PARAM);

	/* show template list for all base OS templates */
	if ((rc = vztt_get_all_base(&base_os)))
		return rc;
        if( opts_vztt->flags & OPT_VZTT_AVAILABLE )
            show_available_templates_list(base_os, opts_vztt);
        else
            for (i = 0; base_os[i]; i++) {
                    if ((rc = show_templates_list(base_os[i], opts_vztt)))
                            return rc;
            }
	return 0;
}

/* print short package information in info command format */
void print_package_list_el(
		struct package *pkg, \
		int with_summary)
{
	char buf[PATH_MAX];
	snprintf(buf, sizeof(buf), "%s.%s", pkg->name, pkg->arch);
	if (with_summary && pkg->descr)
		printf("%-22s %-22s %s\n", buf, pkg->evr, pkg->descr);
	else
		printf("%-40s %s\n", buf, pkg->evr);
}

/* show list */
int cmd_list(
	char *arg0,
	char *arg,
	struct options_vztt *opts_vztt)
{
	ctid_t ctid;
	int rc, i;

	if (is_veid(arg, ctid)) {
		char *ostemplate;
		int tmpl_type;

		/* get ve os template */
		if ((rc = vztt_get_ve_ostemplate(ctid, &ostemplate, &tmpl_type)))
			return rc;

		if (opts_vztt->flags & OPT_VZTT_CUSTOM_PKG) {
			/* get list of `custom` packages:
			packages, installed in VE but not available
			repositories */
			struct package **packages;

			if ((rc = vztt2_get_custom_pkgs(ctid, opts_vztt, &packages)))
				return rc;
			if (packages[0] && !(opts_vztt->flags & OPT_VZTT_QUIET)) {
				printf("The following packages are not "\
					"available in the template "\
					"repositories:\n");
			}
			for (i = 0; packages[i]; i++)
				print_package_list_el(packages[i], \
					opts_vztt->fld_mask & VZTT_INFO_SUMMARY);
		} else if (opts_vztt->flags & OPT_VZTT_VZ_DIR) {
			/* get list of packages directories
			in template area, used for VE */
			char **vzdir;

			if ((rc = vztt2_get_vzdir(ctid, opts_vztt, &vzdir)))
				return rc;
			for (i = 0; vzdir[i]; i++)
				printf("%s\n", vzdir[i]);
		} else if (opts_vztt->objects == OPT_OBJECTS_PACKAGES) {
			/* get list of installed into VE packages */
			struct package **packages;

			if ((rc = vztt2_get_ve_pkgs(ctid, opts_vztt, &packages)))
				return rc;
			for (i = 0; packages[i]; i++)
				print_package_list_el(packages[i], \
					opts_vztt->fld_mask & VZTT_INFO_SUMMARY);
		} else if (opts_vztt->objects == OPT_OBJECTS_GROUPS) {
			struct group_list groups;

			if ((rc = vztt2_get_ve_groups(ctid, opts_vztt, &groups)))
				return rc;
			for (i = 0; groups.installed[i]; i++)
				printf("%s\n", groups.installed[i]);
			vztt_clean_group_list(&groups);
		} else {
			struct tmpl_list_el **ls;
			size_t i;

			if ((rc = vztt2_get_ve_templates_list(ctid, opts_vztt,
					&ls)))
				return rc;
			for (i = 0; ls[i]; i++) {
				if (ls[i]->is_os)
                                    if (opts_vztt->flags & OPT_VZTT_AVAILABLE)
                                        continue;
                                    else
					print_os_tmpl_record(ls[i], opts_vztt);
				else
					print_app_tmpl_record(ostemplate, \
						ls[i], opts_vztt);
			}
			vztt_clean_templates_list(ls);
		}
	} else {
		if (opts_vztt->objects == OPT_OBJECTS_PACKAGES) {
			/* get list of available packages for template */
			struct package **packages;

			if ((rc = vztt2_get_template_pkgs(arg, opts_vztt,
					&packages)))
				return rc;
			for (i = 0; packages[i]; i++)
				print_package_list_el(packages[i], \
					opts_vztt->fld_mask & VZTT_INFO_SUMMARY);
		} else if (opts_vztt->objects == OPT_OBJECTS_GROUPS) {
			struct group_list groups;

			if ((rc = vztt2_get_template_groups(arg, opts_vztt,
					&groups)))
				return rc;
			for (i = 0; groups.available[i]; i++)
				printf("%s\n", groups.available[i]);
			vztt_clean_group_list(&groups);
		} else {
			rc = show_templates_list(arg, opts_vztt);
		}
	}
	return rc;
}

/* print string info field value */
static int print_str_info_opt(
		char *name,
		char *value,
		int todo,
		int quiet)
{
	if (!todo || (value == NULL))
		return 0;

	if (!quiet)
		printf("%s:\n\t", name);
	printf("%s\n", value);

	return 0;
}

/* print name info field value */
static int print_name_info_opt(
		char *name,
		char *value,
		int todo,
		int quiet,
		int pkgid)
{
	if (!todo || (value == NULL))
		return 0;

	if (!quiet)
		printf("%s:\n\t", name);
	if (pkgid)
		printf(".");
	printf("%s\n", value);

	return 0;
}

/* print array info field values */
static int print_arr_info_opt(
		char *name,
		char *arr[],
		int todo,
		int quiet)
{
	size_t i;

	if (!todo || (arr == NULL))
		return 0;
	if (arr[0] == NULL)
		return 0;

	if (!quiet)
		printf("%s:\n", name);
	for (i = 0; arr[i]; i++) {
		if (!quiet)
			printf("\t");
		printf("%s\n", arr[i]);
	}

	return 0;
}

/* print array info field values in one string */
static int print_tech_info_opt(
		char *name,
		char *arr[],
		int todo,
		int quiet)
{
	size_t i;

	if (!todo || (arr == NULL))
		return 0;
	if (arr[0] == NULL)
		return 0;

	if (!quiet)
		printf("%s:\n\t", name);
	for (i = 0; arr[i]; i++)
		printf("%s ", arr[i]);
	printf("\n");

	return 0;
}

/* for info command: convert list of info options to mask */
static int info_field2mask(
		char *info_opts[],
		char *flds[],
		size_t nflds,
		unsigned long *mask)
{
	int lfound;
	char *p;
	int i, j;

	/* check info option if exist */
	for (i = 0; flds[i] && i < nflds; i++) {
		lfound = 0;
		/* for backward compatibility */
		if (strcmp(flds[i], "designation") == 0)
			p = "name";
		else
			p = flds[i];
		/* find in available options list */
		for (j = 0; info_opts[j]; j++) {
			if (strcmp(p, info_opts[j]) == 0) {
				lfound = 1;
				break;
			}
		}
		if (!lfound) {
			vztt_logger(0, 0, "Unknown info option: %s", flds[i]);
			return VZT_BAD_PARAM;
		}
		if (strcmp("name", p) == 0)
			*mask |= VZTT_INFO_NAME;
		else if (strcmp("osname", p) == 0)
			*mask |= VZTT_INFO_OSNAME;
		else if (strcmp("version", p) == 0)
			*mask |= VZTT_INFO_VERSION;
		else if (strcmp("release", p) == 0)
			*mask |= VZTT_INFO_RELEASE;
		else if (strcmp("arch", p) == 0)
			*mask |= VZTT_INFO_ARCH;
		else if (strcmp("config_path", p) == 0)
			*mask |= VZTT_INFO_CONFDIR;
		else if (strcmp("summary", p) == 0)
			*mask |= VZTT_INFO_SUMMARY;
		else if (strcmp("description", p) == 0)
			*mask |= VZTT_INFO_DESCRIPTION;
		else if (strcmp("packages_0", p) == 0)
			*mask |= VZTT_INFO_PACKAGES0;
		else if (strcmp("packages_1", p) == 0)
			*mask |= VZTT_INFO_PACKAGES1;
		else if (strcmp("packages", p) == 0)
			*mask |= VZTT_INFO_PACKAGES;
		else if (strcmp("repositories", p) == 0)
			*mask |= VZTT_INFO_REPOSITORIES;
		else if (strcmp("mirrorlist", p) == 0)
			*mask |= VZTT_INFO_MIRRORLIST;
		else if (strcmp("package_manager", p) == 0)
			*mask |= VZTT_INFO_PACKAGE_MANAGER;
		else if (strcmp("package_manager_type", p) == 0)
			*mask |= VZTT_INFO_PACKAGE_MANAGER_TYPE;
		else if (strcmp("distribution", p) == 0)
			*mask |= VZTT_INFO_DISTRIBUTION;
		else if (strcmp("technologies", p) == 0)
			*mask |= VZTT_INFO_TECHNOLOGIES;
		else if (strcmp("environment", p) == 0)
			*mask |= VZTT_INFO_ENVIRONMENT;
		else if (strcmp("upgradable_versions", p) == 0)
			*mask |= VZTT_INFO_UPGRADABLE_VERSIONS;
		else if (strcmp("cached", p) == 0)
			*mask |= VZTT_INFO_CACHED;
		else if (strcmp("osrelease", p) == 0)
			*mask |= VZTT_INFO_OSRELEASE;
		else if (strcmp("jquota", p) == 0)
			*mask |= VZTT_INFO_JQUOTA;
		else {
			vztt_logger(0, 0, "Unknown info option: %s", p);
			return VZT_BAD_PARAM;
		}
	}

	return 0;
}

/* show package info */
static int show_pkg_info(
		char *package,
		char *flds[],
		size_t nflds,
		struct options_vztt *opts_vztt)
{
	int rc, quiet;
	struct pkg_info **arr;
	size_t i;
	char *pkg_info_opts[] = {"name", "version", "release", "arch", \
				"summary", "description", NULL};

	if (opts_vztt->for_obj == NULL) {
		vztt_logger(0, 0, "--for-os option is needed");
		return VZT_BAD_PARAM;
	}

	/* if option does not specofy or == all - print all */
	if ((nflds == 0) || strcmp(flds[0], "all") == 0) {
		/* get all fields */
		opts_vztt->fld_mask = VZTT_INFO_PKG_ALL;
	} else {
		if ((rc = info_field2mask(pkg_info_opts,
				flds, nflds, &opts_vztt->fld_mask)))
			return rc;
	}

	if ((rc = vztt2_get_pkg_info(opts_vztt->for_obj, package, opts_vztt, &arr)))
		return rc;

	/* quiet is possible only with one option */
	quiet = (nflds == 1) ? (opts_vztt->flags & OPT_VZTT_QUIET) : 0;

	for (i = 0; arr[i] != NULL; i++) {
		print_str_info_opt("name", arr[i]->name,
			opts_vztt->fld_mask & VZTT_INFO_NAME, quiet);
		print_str_info_opt("version", arr[i]->version,
			opts_vztt->fld_mask & VZTT_INFO_VERSION, quiet);
		print_str_info_opt("release", arr[i]->release,
			opts_vztt->fld_mask & VZTT_INFO_RELEASE, quiet);
		print_str_info_opt("arch", arr[i]->arch,
			opts_vztt->fld_mask & VZTT_INFO_ARCH, quiet);
		print_str_info_opt("summary", arr[i]->summary,
			opts_vztt->fld_mask & VZTT_INFO_SUMMARY, quiet);
		print_arr_info_opt("description", arr[i]->description,
			opts_vztt->fld_mask & VZTT_INFO_DESCRIPTION, quiet);
	}
	vztt_clean_pkg_info(&arr);

	return 0;
}

/* show group of packages info */
static int show_group_info(const char *group, struct options_vztt *opts_vztt)
{
	int rc, i;
	struct group_info info;

	if (opts_vztt->for_obj == NULL) {
		vztt_logger(0, 0, "--for-os option is needed");
		return VZT_BAD_PARAM;
	}

	if ((rc = vztt2_get_group_info(opts_vztt->for_obj, group, opts_vztt,
			&info)) == 0) {
		if (info.name) {
			printf("Group: %s\n", info.name);
			for (i = 0; info.list[i]; ++i)
				printf("%s\n", info.list[i]);
		}
	}

	vztt_clean_group_info(&info);

	return rc;
}

/* show template info */
static int show_tmpl_info(
		char *tmpl,
		char *flds[],
		size_t nflds,
		struct options_vztt *opts_vztt)
{
	int rc, quiet;
	struct tmpl_info info;

	/* release option was added for compatibility with previous
	vzpkg only */
	char *tmpl_info_opts[] = {"name", "osname", "version", "arch", \
		"config_path", "summary", "description", "packages_0", \
		"packages_1", "packages", "repositories", "mirrorlist", \
		"package_manager", "package_manager_type", "distribution", \
		"technologies", "environment", "upgradable_versions", \
		"release", "cached", "osrelease", "jquota", NULL};

	/* if option does not specofy or == all - print all */
	if ((nflds == 0) || strcmp(flds[0], "all") == 0) {
		/* get all fields */
		opts_vztt->fld_mask = VZTT_INFO_TMPL_ALL;
	} else {
		if ((rc = info_field2mask(tmpl_info_opts,
				flds, nflds, &opts_vztt->fld_mask)))
			return rc;
	}

	if (opts_vztt->for_obj) {
		if ((rc = vztt2_get_app_tmpl_info(opts_vztt->for_obj, tmpl, \
				opts_vztt, &info)))
			return rc;
	} else {
		if ((rc = vztt2_get_os_tmpl_info(tmpl, opts_vztt, &info)))
			return rc;
	}

	/* quiet is possible only with one option */
	quiet = (nflds == 1) ? (opts_vztt->flags & OPT_VZTT_QUIET) : 0;

	print_name_info_opt("name", info.name,
		opts_vztt->fld_mask & VZTT_INFO_NAME, quiet,
		(opts_vztt->flags & OPT_VZTT_PKGID) ? 1 : 0);
	print_str_info_opt("osname", info.osname,
		opts_vztt->fld_mask & VZTT_INFO_OSNAME, quiet);
	print_str_info_opt("version", info.osver,
		opts_vztt->fld_mask & VZTT_INFO_VERSION, quiet);
	print_str_info_opt("arch", info.osarch,
		opts_vztt->fld_mask & VZTT_INFO_ARCH, quiet);
	print_str_info_opt("config_path", info.confdir,
		opts_vztt->fld_mask & VZTT_INFO_CONFDIR, quiet);
	print_str_info_opt("summary", info.summary,
		opts_vztt->fld_mask & VZTT_INFO_SUMMARY, quiet);
	print_arr_info_opt("description", info.description,
		opts_vztt->fld_mask & VZTT_INFO_DESCRIPTION, quiet);
	print_arr_info_opt("packages_0", info.packages0,
		opts_vztt->fld_mask & VZTT_INFO_PACKAGES0, quiet);
	print_arr_info_opt("packages_1", info.packages1,
		opts_vztt->fld_mask & VZTT_INFO_PACKAGES1, quiet);
	print_arr_info_opt("packages", info.packages,
		opts_vztt->fld_mask & VZTT_INFO_PACKAGES, quiet);
	print_arr_info_opt("repositories", info.repositories,
		opts_vztt->fld_mask & VZTT_INFO_REPOSITORIES, quiet);
	print_arr_info_opt("mirrorlist", info.mirrorlist,
		opts_vztt->fld_mask & VZTT_INFO_MIRRORLIST, quiet);
	print_str_info_opt("package_manager", info.package_manager,
		opts_vztt->fld_mask & VZTT_INFO_PACKAGE_MANAGER, quiet);
	print_str_info_opt("package_manager_type", info.package_manager_type,
		opts_vztt->fld_mask & VZTT_INFO_PACKAGE_MANAGER_TYPE, quiet);
	print_str_info_opt("distribution", info.distribution,
		opts_vztt->fld_mask & VZTT_INFO_DISTRIBUTION, quiet);
	print_tech_info_opt("technologies", info.technologies,
		opts_vztt->fld_mask & VZTT_INFO_TECHNOLOGIES, quiet);
	print_arr_info_opt("environment", info.environment,
		opts_vztt->fld_mask & VZTT_INFO_ENVIRONMENT, quiet);
	print_arr_info_opt("upgradable_versions", info.upgradable_versions,
		opts_vztt->fld_mask & VZTT_INFO_UPGRADABLE_VERSIONS, quiet);
	print_str_info_opt("cached", info.cached,
		opts_vztt->fld_mask & VZTT_INFO_CACHED, quiet);
	if ((opts_vztt->fld_mask & VZTT_INFO_OSRELEASE) &&
		!(opts_vztt->fld_mask & VZTT_INFO_DISTRIBUTION))
		print_str_info_opt("osrelease", info.distribution,
			opts_vztt->fld_mask & VZTT_INFO_OSRELEASE, quiet);
	if ((opts_vztt->fld_mask & VZTT_INFO_JQUOTA) &&
		!(opts_vztt->fld_mask & VZTT_INFO_DISTRIBUTION))
		print_str_info_opt("jquota", info.distribution,
			opts_vztt->fld_mask & VZTT_INFO_JQUOTA, quiet);
	vztt_clean_tmpl_info(&info);
	return 0;
}

/* print list of modified packages with title */
static void print_package_list(
		const char *title,
		struct package **packages)
{
	int i;

	if (packages == NULL)
		return;
	if (packages[0] == NULL)
		return;

	printf("%s\n", title);
	for (i = 0; packages[i]; i++) {
		printf(" %-22s %-9s %s\n", \
				packages[i]->name, \
				packages[i]->arch, \
				packages[i]->evr);
	}
}

int main(int argc, char **argv)
{
	int rc = 0;
	vztt_cmd_t command = VZTT_CMD_NONE;
	struct options_vztt *opts_vztt;
	ctid_t ctid;
	int ncmd = 1, ind, i;
	char **base_os;
	int lvzctl_open = 0;

	umask(022);

	if (getuid()) {
		vztt_logger(0, 0, "This program should run only from root");
		return VZT_USER_NOT_ROOT;
	}
	if (argc < 2)
		usage(argv[0], VZT_BAD_PARAM);

	command = VZTT_CMD_NONE;
	if (argc > 2) {
		ncmd = 2;
		if ((strcmp(argv[1], "create") == 0)) {
			if (strcmp(argv[2], "cache") == 0)
				command = VZTT_CMD_CREATE_CACHE;
			else if (strcmp(argv[2], "appcache") == 0)
				command = VZTT_CMD_CREATE_APPCACHE;
			else if (strcmp(argv[2], "image") == 0)
				command = VZTT_CMD_CREATE_PLOOP_IMAGE;
		} else if (strcmp(argv[1], "update") == 0) {
			if (strcmp(argv[2], "cache") == 0)
				command = VZTT_CMD_UPDATE_CACHE;
			else if (strcmp(argv[2], "appcache") == 0)
				command = VZTT_CMD_UPDATE_APPCACHE;
			else if (strcmp(argv[2], "metadata") == 0)
				command = VZTT_CMD_UPDATE_METADATA;
			else if (strcmp(argv[2], "template") == 0)
				command = VZTT_CMD_UPDATE_TEMPLATE;
		} else if (strcmp(argv[1], "remove") == 0) {
			if (strcmp(argv[2], "cache") == 0)
				command = VZTT_CMD_REMOVE_CACHE;
			else if (strcmp(argv[2], "appcache") == 0)
				command = VZTT_CMD_REMOVE_APPCACHE;
			else if (strcmp(argv[2], "template") == 0)
				command = VZTT_CMD_REMOVE_TEMPLATE;
		} else if ((strcmp(argv[1], "list") == 0) &&
			(strcmp(argv[2], "appcache") == 0)) {
				command = VZTT_CMD_LIST_APPCACHE;
		} else if ((strcmp(argv[1], "install") == 0) &&\
				(strcmp(argv[2], "template") == 0)) {
			command = VZTT_CMD_INSTALL_TEMPLATE;
		} else if ((strcmp(argv[1], "upgrade") == 0) &&\
				(strcmp(argv[2], "area") == 0)) {
			command = VZTT_CMD_UPGRADE_AREA;
		} else if ((strcmp(argv[1], "verify") == 0) &&\
				(strcmp(argv[2], "area") == 0)) {
			command = VZTT_CMD_GET_AREA_VZFS;
		}
	}
	if (command == VZTT_CMD_NONE) {
		ncmd = 1;
		if (strcmp(argv[1], "list") == 0)
			command = VZTT_CMD_LIST;
		else if (strcmp(argv[1], "repair") == 0)
			command = VZTT_CMD_REPAIR;
		else if (strcmp(argv[1], "upgrade") == 0)
			command = VZTT_CMD_UPGRADE;
		else if (strcmp(argv[1], "clean") == 0)
			command = VZTT_CMD_CLEAN;
		else if (strcmp(argv[1], "status") == 0)
			command = VZTT_CMD_STATUS;
		else if (strcmp(argv[1], "install") == 0)
			command = VZTT_CMD_INSTALL;
		else if (strcmp(argv[1], "update") == 0)
			command = VZTT_CMD_UPDATE;
		else if (strcmp(argv[1], "remove") == 0)
			command = VZTT_CMD_REMOVE;
		else if (strcmp(argv[1], "localinstall") == 0)
			command = VZTT_CMD_LOCALINSTALL;
		else if (strcmp(argv[1], "localupdate") == 0)
			command = VZTT_CMD_LOCALUPDATE;
		else if (strcmp(argv[1], "link") == 0)
			command = VZTT_CMD_LINK;
		else if (strcmp(argv[1], "fetch") == 0)
			command = VZTT_CMD_FETCH;
		else if (strcmp(argv[1], "info") == 0)
			command = VZTT_CMD_INFO;
		else if (strcmp(argv[1], "get_backup_apps") == 0)
			command = VZTT_CMD_GET_BACKUP_APPS;
		else if (strcmp(argv[1], "sync_vzpackages") == 0)
			command = VZTT_CMD_SYNC_VZPACKAGES;
		else if (strcmp(argv[1], "help") == 0)
			command = VZTT_CMD_HELP;
		else
			usage(argv[0], VZT_BAD_PARAM);
	}

	opts_vztt = vztt_options_create();
	if (!opts_vztt)
		return VZT_CANT_ALLOC_MEM;

	/* Need to distinguish 4 situations for update: no options, -p, -t and -g */
	if (command == VZTT_CMD_UPDATE)
		opts_vztt->objects = OPT_OBJECTS_NONE;

	if ((rc = parse_cmd_line(argc, argv, opts_vztt)))
		usage(argv[0], rc);

        if( (opts_vztt->flags & OPT_VZTT_AVAILABLE) && (command != VZTT_CMD_LIST ) )
		usage(argv[0], VZT_BAD_PARAM);

	debug_level = opts_vztt->debug;

	vztt_init_logger(opts_vztt->logfile, opts_vztt->debug);

	if (!(opts_vztt->flags & OPT_VZTT_FORCE_VZCTL)) {
		/* Get vz service status */
		rc = vzctl2_vz_status();
		if (rc == -1) {
			vztt_logger(0, 0, "Can not get " PRODUCT_NAME_SHORT " service status");
			return VZT_CANT_GET_VZ_STATUS;
		} else if (rc != 1) {
			vztt_logger(0, 0, "The vz service is not running");
			return VZT_VZ_NOT_RUNNING;
		}
		/* check vzctl */
		if (access(VZCTL, X_OK)) {
			vztt_logger(0, 0, "%s not found or is not executable", VZCTL);
			return VZT_VZCTL_NFOUND;
		}
	}
	/* vzctl2_vz_status() return 1 for running vzctl */
	rc = 0;

	setlocale(LC_ALL,"C");
	/* set timezone */
	tzset();

	ind = optind + ncmd;
	if 		((command == VZTT_CMD_CREATE_CACHE) || \
			(command == VZTT_CMD_UPDATE_CACHE) || \
			(command == VZTT_CMD_REMOVE_CACHE) || \
			(command == VZTT_CMD_UPDATE_METADATA) || \
			(command == VZTT_CMD_REMOVE_TEMPLATE) || \
			(command == VZTT_CMD_INSTALL) || \
			(command == VZTT_CMD_UPDATE) || \
			(command == VZTT_CMD_REMOVE) || \
			(command == VZTT_CMD_LIST) || \
			(command == VZTT_CMD_REPAIR) || \
			(command == VZTT_CMD_CLEAN) || \
			(command == VZTT_CMD_STATUS) || \
			(command == VZTT_CMD_FETCH) || \
			(command == VZTT_CMD_INFO)) {
		for (i = ind; argv[i]; i++) {
			/* remove leading dots */
			if (*(argv[i]) == '.') argv[i] += 1;
		}
	}
	if ((i = vzctl2_lib_init())) {
		vztt_logger(0, 0, "vzctl2_lib_init() error %s", vzctl2_get_last_error());
		if (!(opts_vztt->flags & OPT_VZTT_FORCE_VZCTL))
			return VZT_VZCTL_ERROR;
	} else {
		lvzctl_open = 1;
	/*
		vzctl2_set_log_file() open file for append
		vzctl2_set_log_file(opts_vztt->logfile);
	*/
		vzctl2_set_log_quiet((opts_vztt->flags & OPT_VZTT_QUIET));
	}

	switch(command) {
	case VZTT_CMD_LIST:
		if (argc <= ind) {
			rc = cmd_list_all_templates(argv[0], opts_vztt);
		} else {
			for (i = ind; argv[i] && (rc == 0); i++)
				rc = cmd_list(argv[0], argv[i], opts_vztt);
		}
		break;
	case VZTT_CMD_UPGRADE:
	{
		struct package **pkg_updated = NULL;
		struct package **pkg_added = NULL;
		struct package **pkg_removed = NULL;
		struct package **pkg_converted = NULL;

		if (argc <= ind)
			usage(argv[0], VZT_BAD_PARAM);
		for (i = ind; argv[i] && (rc == 0); i++)
			rc = vztt2_upgrade(argv[i], opts_vztt, \
				&pkg_updated, &pkg_added, \
				&pkg_removed, &pkg_converted);
		if (rc == 0) {
			print_package_list("Updated:", pkg_updated);
			print_package_list("Installed:", pkg_added);
			print_package_list("Removed:", pkg_removed);
			print_package_list("Converted:", pkg_converted);
		}
		break;
	}
	case VZTT_CMD_REPAIR:
		if (argc <= ind+1)
			usage(argv[0], VZT_BAD_PARAM);
		rc = vztt2_repair(argv[ind], argv[ind+1], opts_vztt);
		break;
	case VZTT_CMD_CLEAN:
		if (argc == ind) {
			/* for all base OS template */
			if ((rc = vztt_get_all_base(&base_os)))
				goto cleanup;
			for (i = 0; base_os[i]; i++) {
				if ((rc = vztt2_cleanup(base_os[i], opts_vztt)))
					goto cleanup;
			}
		} else {
			for (i = ind; argv[i] && (rc == 0); i++)
				rc = vztt2_cleanup(argv[i], opts_vztt);
		}
		break;
	case VZTT_CMD_STATUS:
		if (argc == ind) {
			/* for all base OS template */
			if ((rc = vztt_get_all_base(&base_os)))
				goto cleanup;
			for (i = 0; base_os[i]; i++) {
				if ((rc = vztt2_get_cache_status(base_os[i], \
						opts_vztt)))
					goto cleanup;
			}
		} else {
			for (i = ind; argv[i] && (rc == 0); i++) {
				if (is_veid(argv[i], ctid)) {
					rc = vztt2_get_ve_status(ctid, opts_vztt);
				} else {
					rc = vztt2_get_cache_status(argv[i], \
						opts_vztt);
				}
			}
		}
		break;
	case VZTT_CMD_UPDATE_METADATA:
		if (opts_vztt->data_source == OPT_DATASOURCE_LOCAL) {
			vztt_logger(0, 0, "-C/--cache option is not "
				"valid for update metadata command");
			rc = VZT_BAD_PARAM;
			break;
		}
		/* already update metadata, ignore METADATA_EXPIRE (#113985) */
		opts_vztt->data_source = OPT_DATASOURCE_REMOTE;
		if (argc == ind) {
			/* for all base OS template */
			if ((rc = vztt_get_all_base(&base_os)))
				goto cleanup;
			for (i = 0; base_os[i]; i++) {
				if ((rc = vztt2_update_metadata(base_os[i], \
						opts_vztt)))
					goto cleanup;
			}
		} else {
			for (i = ind; argv[i] && (rc == 0); i++)
				rc = vztt2_update_metadata(argv[i], opts_vztt);
		}
		break;
	case VZTT_CMD_INSTALL:
	{
		char *ostemplate;
		int tmpl_type;
		struct package **pkg_added = NULL;
		struct package **pkg_removed = NULL;

		if (argc <= ind+1)
			usage(argv[0], VZT_BAD_PARAM);

		if ((rc = get_veid(argv[ind], ctid)))
			goto cleanup;

		/* get ve os template */
		if ((rc = vztt_get_ve_ostemplate(ctid, &ostemplate, &tmpl_type)))
			goto cleanup;

		if (opts_vztt->objects == OPT_OBJECTS_PACKAGES) {
			rc = vztt2_install(ctid, &argv[ind+1], argc-ind-1, \
					opts_vztt, &pkg_added, &pkg_removed);
		} else if (opts_vztt->objects == OPT_OBJECTS_GROUPS) {
			rc = vztt2_install_group(ctid, &argv[ind+1],
					argc-ind-1, opts_vztt, &pkg_added,
					&pkg_removed);
		} else {
			rc = vztt2_install_tmpl(ctid, &argv[ind+1], argc-ind-1,\
					opts_vztt, &pkg_added, &pkg_removed);
		}
		if (rc == 0) {
			print_package_list("Installed:", pkg_added);
			print_package_list("Removed:", pkg_removed);
		}
		break;
	}
	case VZTT_CMD_UPDATE:
	{
		/* for update packages/template number may be 0 */
		struct package **pkg_updated = NULL;
		struct package **pkg_removed = NULL;

		if (argc <= ind)
			usage(argv[0], VZT_BAD_PARAM);

		if ((rc = get_veid(argv[ind], ctid)))
			goto cleanup;

		if (opts_vztt->objects == OPT_OBJECTS_NONE) {
			if (argc-ind-1 == 0)
				/* vzpkg update <VEID> will always update
				   _all_ packages into VE (#85076) */
				opts_vztt->objects = OPT_OBJECTS_PACKAGES;
			else
				opts_vztt->objects = OPT_OBJECTS_TEMPLATES;
		}

		if (opts_vztt->objects == OPT_OBJECTS_PACKAGES)
			rc = vztt2_update(ctid, &argv[ind+1], argc-ind-1,
					opts_vztt, &pkg_updated, &pkg_removed);
		else if (opts_vztt->objects == OPT_OBJECTS_GROUPS)
			rc = vztt2_update_group(ctid, &argv[ind+1], argc-ind-1,
					opts_vztt, &pkg_updated, &pkg_removed);
		else
			rc = vztt2_update_tmpl(ctid, &argv[ind+1], argc-ind-1,
					opts_vztt, &pkg_updated, &pkg_removed);
		if (rc == 0) {
			print_package_list("Updated:", pkg_updated);
			print_package_list("Removed:", pkg_removed);
		}
		break;
	}
	case VZTT_CMD_REMOVE:
	{
		struct package **pkg_removed = NULL;

		if (argc <= ind+1)
			usage(argv[0], VZT_BAD_PARAM);

		if ((rc = get_veid(argv[ind], ctid)))
			goto cleanup;

		if (opts_vztt->objects == OPT_OBJECTS_PACKAGES)
			rc = vztt2_remove(ctid, &argv[ind+1], argc-ind-1, opts_vztt, &pkg_removed);
		else if (opts_vztt->objects == OPT_OBJECTS_GROUPS)
			rc = vztt2_remove_group(ctid, &argv[ind+1], argc-ind-1, opts_vztt, &pkg_removed);
		else
			rc = vztt2_remove_tmpl(ctid, &argv[ind+1], argc-ind-1, opts_vztt, &pkg_removed);
		if (rc == 0) {
			print_package_list("Removed:", pkg_removed);
		}
		break;
	}
	case VZTT_CMD_LOCALINSTALL:
	{
		struct package **pkg_added = NULL;
		struct package **pkg_removed = NULL;

		if (argc <= ind+1)
			usage(argv[0], VZT_BAD_PARAM);

		if ((rc = get_veid(argv[ind], ctid)))
			goto cleanup;

		rc = vztt2_localinstall(ctid, &argv[ind+1], argc-ind-1, \
					opts_vztt, &pkg_added, &pkg_removed);
		if (rc == 0) {
			print_package_list("Installed:", pkg_added);
			print_package_list("Removed:", pkg_removed);
		}
		break;
	}
	case VZTT_CMD_LOCALUPDATE:
	{
		struct package **pkg_updated = NULL;
		struct package **pkg_removed = NULL;

		if (argc <= ind+1)
			usage(argv[0], VZT_BAD_PARAM);

		if ((rc = get_veid(argv[ind], ctid)))
			goto cleanup;

		rc = vztt2_localupdate(ctid, &argv[ind+1], argc-ind-1, \
					opts_vztt, &pkg_updated, &pkg_removed);
		if (rc == 0) {
			print_package_list("Updated:", pkg_updated);
			print_package_list("Removed:", pkg_removed);
		}
		break;
	}
	case VZTT_CMD_CREATE_CACHE:
		if (argc == ind) {
			/* for all base OS template */
			if ((rc = vztt_get_all_base(&base_os)))
				goto cleanup;
			for (i = 0; base_os[i]; i++) {
				if ((rc = vztt2_create_cache(base_os[i],
						opts_vztt, OPT_CACHE_SKIP_EXISTED)))
					goto cleanup;
			}
		} else {
			for (i = ind; argv[i] && (rc == 0); i++)
				rc = vztt2_create_cache(argv[i], opts_vztt, OPT_CACHE_SKIP_EXISTED);
		}
		break;
	case VZTT_CMD_UPDATE_CACHE:
		if (argc == ind) {
			/* for all base OS template */
			if ((rc = vztt_get_all_base(&base_os)))
				goto cleanup;
			for (i = 0; base_os[i]; i++) {
				if ((rc = vztt2_update_cache(base_os[i], opts_vztt)))
					goto cleanup;
			}
		} else {
			for (i = ind; argv[i] && (rc == 0); i++)
				rc = vztt2_update_cache(argv[i], opts_vztt);
		}
		break;
	case VZTT_CMD_REMOVE_CACHE:
		if (argc == ind) {
			/* for all base OS template */
			if ((rc = vztt_get_all_base(&base_os)))
				goto cleanup;
			for (i = 0; base_os[i]; i++) {
				if ((rc = vztt2_remove_cache(base_os[i], opts_vztt)))
					goto cleanup;
			}
		} else {
			for (i = ind; argv[i] && (rc == 0); i++)
				rc = vztt2_remove_cache(argv[i], opts_vztt);
		}
		break;
	case VZTT_CMD_LINK:
		if (argc <= ind)
			usage(argv[0], VZT_BAD_PARAM);

		for (i = ind; argv[i] && (rc == 0); i++) {
			if ((rc = get_veid(argv[i], ctid)))
				goto cleanup;

			rc = vztt2_link(ctid, opts_vztt);
		}
		break;
	case VZTT_CMD_FETCH:
		if (argc <= ind)
			usage(argv[0], VZT_BAD_PARAM);

		for (i = ind; argv[i] && (rc == 0); i++)
			if (opts_vztt->flags & OPT_VZTT_SEPARATE)
				rc = vztt2_fetch_separately(argv[i], opts_vztt);
			else
				rc = vztt2_fetch(argv[i], opts_vztt);
		break;
	case VZTT_CMD_INFO:
	{
		if ((opts_vztt->config) || (opts_vztt->app_ostemplate) ||
				(opts_vztt->app_apptemplate))
		{
			rc = info_appcache(opts_vztt);
			break;
		}

		if (argc <= ind)
			usage(argv[0], VZT_BAD_PARAM);

		if (opts_vztt->objects == OPT_OBJECTS_PACKAGES) {
			/* show package info */
			rc = show_pkg_info(argv[ind], &argv[ind+1], argc-ind-1, opts_vztt);
		} else if (opts_vztt->objects == OPT_OBJECTS_GROUPS) {
			/* show group of packages info */
			rc = show_group_info(argv[ind], opts_vztt);
		} else {
			/* show template info */
			rc = show_tmpl_info(argv[ind], &argv[ind+1], \
				argc-ind-1, opts_vztt);
		}

		break;
	}
	case VZTT_CMD_INSTALL_TEMPLATE:
	{
		char **ls;
		int i;
		if (argc <= ind)
			usage(argv[0], VZT_BAD_PARAM);

		if ((rc = vztt2_install_template(&argv[ind], argc-ind,
				opts_vztt, &ls)) == 0) {
			for (i = 0; ls[i]; i++)
				vztt_logger(VZTL_INFO, 0,
					"%s template was installed", ls[i]);
		}
		break;
	}
	case VZTT_CMD_UPDATE_TEMPLATE:
	{
		char **ls;
		int i;
		if (argc <= ind)
			usage(argv[0], VZT_BAD_PARAM);

		if ((rc = vztt2_update_template(&argv[ind], argc-ind,
				opts_vztt, &ls)) == 0) {
			for (i = 0; ls[i]; i++)
				vztt_logger(VZTL_INFO, 0,
					"%s template was updated", ls[i]);
		}
		break;
	}
	case VZTT_CMD_REMOVE_TEMPLATE:
	{
		if (argc <= ind)
			usage(argv[0], VZT_BAD_PARAM);

		if (opts_vztt->for_obj == NULL) {
			for (i = ind; argv[i] && (rc == 0); i++) {
				if ((rc = vztt2_remove_os_template(argv[i],
						opts_vztt)))
					break;
				vztt_logger(VZTL_INFO, 0,
					"%s template was removed", argv[i]);
			}
		} else {
			for (i = ind; argv[i] && (rc == 0); i++) {
				if ((rc = vztt2_remove_app_template(argv[i],
						opts_vztt)))
					break;
				vztt_logger(VZTL_INFO, 0,
					"%s %s template was removed",
					opts_vztt->for_obj, argv[i]);
			}
		}
		break;
	}
	case VZTT_CMD_UPGRADE_AREA:
		/* Upgrade template area from vzfs3 to vzfs4 */
		if (argc == ind) {
			/* for all base OS template */
			if ((rc = vztt_get_all_base(&base_os)))
				goto cleanup;
			for (i = 0; base_os[i] && (rc == 0); i++)
				rc = vztt2_upgrade_area(base_os[i], opts_vztt);
		} else {
			for (i = ind; argv[i] && (rc == 0); i++)
				rc = vztt2_upgrade_area(argv[i], opts_vztt);
		}
		break;
	case VZTT_CMD_GET_AREA_VZFS:
		/* check vzfs version of directories in template area,
		used for VE <veid> */
		if (argc <= ind)
			usage(argv[0], VZT_BAD_PARAM);

		for (i = ind; argv[i] && (rc == 0); i++) {
			if ((rc = get_veid(argv[i], ctid)))
				goto cleanup;

			rc = vztt2_check_vzdir(ctid, opts_vztt);
		}
		break;
	case VZTT_CMD_GET_BACKUP_APPS:
	{
		if (argc <= ind+1)
			usage(argv[0], VZT_BAD_PARAM);
		/* for backup: get list of installed apptemplates
		   vzpackages list from <veprivate>
		   and template variables from <veconfig> */
		char **apps;

		if ((rc = vztt2_get_backup_apps(argv[ind], argv[ind+1], \
					opts_vztt, &apps)))
				goto cleanup;
		for (i = 0; apps[i]; i++)
			printf("%s\n", apps[i]);
		break;
	}
	case VZTT_CMD_SYNC_VZPACKAGES:
		/* To read VE package manager database and
		   correct file vzpackages & app templates list */
		if (argc <= ind)
			usage(argv[0], VZT_BAD_PARAM);
		if ((rc = get_veid(argv[ind], ctid)))
			goto cleanup;
		if ((rc = vztt2_sync_vzpackages(ctid, opts_vztt)))
			goto cleanup;
		break;
	case VZTT_CMD_HELP:
		usage(argv[0], 0);
		break;
	case VZTT_CMD_CREATE_APPCACHE:
		rc = vztt2_create_appcache(opts_vztt, 0);
		break;
	case VZTT_CMD_UPDATE_APPCACHE:
		rc = vztt2_update_appcache(opts_vztt);
		break;
	case VZTT_CMD_REMOVE_APPCACHE:
		rc = vztt2_remove_appcache(opts_vztt);
		break;
	case VZTT_CMD_LIST_APPCACHE:
		rc = vztt2_list_appcache(opts_vztt);
		break;
	case VZTT_CMD_CREATE_PLOOP_IMAGE: {
		struct vzctl_create_image_param p = {}; 
		p.timeout = opts_vztt->timeout;

		if (argv[ind] == NULL || argv[ind + 1] == NULL)
			usage(argv[0], VZT_BAD_PARAM);

		if (vzctl2_prepare_root_image(argv[ind+1], argv[ind], &p))
			rc = VZT_CANT_CREATE;
		break;
	}
	case VZTT_CMD_NONE:
		usage(argv[0], VZT_BAD_PARAM);
		break;
	}

cleanup:
	if (lvzctl_open)
		vzctl2_lib_close();

	return rc;
}
