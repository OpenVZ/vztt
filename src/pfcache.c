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
 * pfcache operations module
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
#include <attr/xattr.h>
#include <dirent.h>
#include <error.h>
#include <limits.h>
#include <signal.h>
#include <sys/wait.h>
#include <utime.h>
#include <asm/unistd.h>
#include <sys/mount.h>
#include <linux/magic.h>
#include <sys/vfs.h>

#include "vztt_error.h"
#include "util.h"
#include "queue.h"
#include "config.h"

#ifndef PFCACHE_XATTR_NAME
#define PFCACHE_XATTR_NAME	"trusted.pfcache"
#endif

#define PFCACHE_XATTR_AUTO	"auto"

/* set 'trusted' xattr on directory (https://jira.sw.ru/browse/PSBM-10447) */
static int pfcache_set_trusted_xattr(
		const char *root,
		struct string_list *whitelist)
{
	int rc;

	/* if 'while list' is empty - do not calculate csums at all */
	if (string_list_empty(whitelist))
		return 0;

	if ((rc = setxattr( root, PFCACHE_XATTR_NAME,
		       PFCACHE_XATTR_AUTO, strlen(PFCACHE_XATTR_AUTO), 0 )))
		vztt_logger(0, 0, "setxattr on %s failed: %s", root, strerror(errno));

	return rc;
}

/* input:
 * CT root
 * relative path from CT root with final slash
 * name of directory item
 * list of excludes
 * output:
 * list of relative pathes of directories with final slash
 */
static int add_directory_to_list(
		const char *root,
		const char *relpath,
		const char *name,
		struct string_list *excludes,
		struct string_list *ls)
{
	char rpath[PATH_MAX+1];
	char path[PATH_MAX+1];
	struct stat st;

	if (strcmp(".", name) == 0)
		return 0;
	if (strcmp("..", name) == 0)
		return 0;

	/* compare relative path with tailing slash */
	snprintf(rpath, sizeof(rpath), "%s%s/", relpath, name);
	if (string_list_find(excludes, rpath) != NULL)
		return 0;

	/* is not in white list - clear trusted xattr at the first */
	snprintf(path, sizeof(path), "%s/%s%s", root, relpath, name);
	/* will ignore removexattr errors */
	removexattr( path, PFCACHE_XATTR_NAME );

	/* skip non-directories */
	if (lstat(path, &st))
		return vztt_error(VZT_CANT_LSTAT, errno, "stat(%s) : %m", path);
	if (!S_ISDIR(st.st_mode))
		return 0;

	// setfattr ignores final slash for directory
	return string_list_add_head(ls, rpath);
}

static int read_directory(
		const char *root,
		const char *relpath,
		struct string_list *excludes,
		struct string_list *ls)
{
	int rc = 0;
	DIR *dir;
	char dirent_buf[sizeof(struct dirent) + PATH_MAX + 1];
	struct dirent *de = (struct dirent *) dirent_buf;
	struct dirent *result;
	int retval;
	char path[PATH_MAX+1];
	struct statfs stfs;

	snprintf(path, sizeof(path), "%s/%s", root, relpath);
	// skip non-ext3 FS (https://jira.sw.ru/browse/PSBM-13949)
	if (statfs(path, &stfs))
		return vztt_error(VZT_SYSTEM, errno, "statfs(%s) error", path);
	if (stfs.f_type != EXT3_SUPER_MAGIC)
		return 0;
	if ((dir = opendir(path)) == NULL)
		return vztt_error(VZT_CANT_OPEN, errno, "opendir(%s) error", path);

	while (rc == 0) {
		if ((retval = readdir_r(dir, de, &result)) != 0)
		{
			rc = vztt_error(VZT_CANT_READ, retval, "readdir_r(%s) error", path);
			break;
		}

		if (result == NULL)
			break;

		rc = add_directory_to_list(root, relpath, de->d_name, excludes, ls);
	}
	closedir(dir);
	return rc;
}

/* clear 'trusted' xattr from directories missing in 'white list'
 * (https://jira.sw.ru/browse/PSBM-10447) */
static int pfcache_clear_trusted_xattr(
		const char *root,
		struct string_list *excludes)
{
	int rc = 0;
	DIR *dir;
	char dirent_buf[sizeof(struct dirent) + PATH_MAX + 1];
	struct dirent *de = (struct dirent *) dirent_buf;
	struct dirent *result;
	int retval;

	struct string_list ls;
	struct string_list_el *p;

	if (string_list_empty(excludes))
		return 0;

	string_list_init(&ls);
	/* clear root itself */
	removexattr( root, PFCACHE_XATTR_NAME );

	if ((dir = opendir(root)) == NULL) {
		vztt_logger(0, errno, "opendir(%s) error", root);
		return VZT_CANT_OPEN;
	}

	while (rc == 0) {
		if ((retval = readdir_r(dir, de, &result)) != 0)
		{
			vztt_logger(0, retval, "readdir_r(\"%s\") error",
					root);
			rc = VZT_CANT_READ;
			break;
		}

		if (result == NULL)
			break;

		rc = add_directory_to_list(root, "", de->d_name, excludes, &ls);
		if (rc)
			break;

		for (p = TAILQ_FIRST(&ls); p; p = TAILQ_FIRST(&ls)) {
			if ((rc = read_directory(root, p->s, excludes, &ls)))
				break;
			string_list_remove(&ls, p);
		}
	}
	closedir(dir);
	string_list_clean(&ls);
	return rc;
}

void usage(const char * progname)
{
    fprintf(stderr,"\nUsage: %s set|clear root\n", progname);
}

int main(int argc, char **argv)
{
	int rc;
	char *cmd;
	char *root;
	struct global_config gc;

	if (argc != 3) {
		usage(argv[0]);
		return 1;
	}
	cmd = argv[1];
	root = argv[2];

	global_config_init(&gc);

	/* read global vz config */
	if ((rc = global_config_read(&gc, 0)))
		return rc;

	if (strcmp(cmd, "set") == 0) {
		rc = pfcache_set_trusted_xattr(root, &gc.csum_white_list);
	} else if (strcmp(cmd, "clear") == 0) {
		rc = pfcache_clear_trusted_xattr(root, &gc.csum_white_list);
	} else {
		usage(argv[0]);
		return 1;
	}
	global_config_clean(&gc);

	return rc;
}
