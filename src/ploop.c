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
 * ploop operations module
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
#include <ploop/libploop.h>
#include <ploop/ploop_if.h>

#include <vzctl/libvzctl.h>

#include "vztt.h"
#include "util.h"
#include "progress_messages.h"
#include "ploop.h"
#include "transaction.h"

#define IMAGE_NAME		"root.hds"
#define DESCRIPTOR_NAME		"DiskDescriptor.xml"

static int copy_data(
	const char *from,
	const char *dir,
	const char*to,
	struct options_vztt *opts_vztt)
{
	int rc = 0;
	char cmd[PATH_MAX];

	snprintf(cmd, sizeof(cmd), TAR " -f - -cpS -C %s %s | " \
		TAR " -f - -xSC %s; [ ${PIPESTATUS[0]:-1} -eq 0 -a " \
		"${PIPESTATUS[1]:-1} -eq 0 ] || exit 1", from, dir, to);
	if ((rc = exec_cmd(cmd, (opts_vztt->flags & OPT_VZTT_QUIET)))) {
		vztt_logger(0, errno, "system(%s) error", cmd);
		rc = VZT_CANT_EXEC;
	}

	return rc;
}

static int open_ploop_di(char *ploop_dir, struct ploop_disk_images_data **di)
{
	int rc = 0;
	char path[PATH_MAX+1];

	snprintf(path, sizeof(path), "%s/"DESCRIPTOR_NAME,
			ploop_dir);

	if ((rc = ploop_open_dd(di, path)))
	{
		vztt_logger(0, 0, "Failed to read ploop disk descriptor %s: %s %i",
			ploop_dir, ploop_get_last_error(), rc);
		rc = VZT_PLOOP_ERROR;
	}

	return rc;
}

int convert_ploop(const char *old_cache, char *cache,
		struct options_vztt *opts_vztt)
{
	int rc;
	char cmd[PATH_MAX];
	char from[PATH_MAX];
	char to[PATH_MAX];
	char *old_tmpdir = NULL;
	int old_mounted = 0;
	char *new_tmpdir = NULL;
	int new_mounted = 0;
	struct ploop_disk_images_data *di = 0;

	vztt_logger(1, 0, "Converting %s to %s", old_cache, cache);
	progress(PROGRESS_CREATE_PLOOP, 0, opts_vztt->progress_fd);

	/* 1. Unpack archive */
	rc = create_tmp_dir(&old_tmpdir);
	if (rc)
		return rc;
	get_unpack_cmd(cmd, sizeof(cmd), old_cache, old_tmpdir, "--numeric-owner");
	if ((rc = exec_cmd(cmd, (opts_vztt->flags & OPT_VZTT_QUIET)))) {
		vztt_logger(0, errno, "system(%s) error", cmd);
		rc = VZT_CANT_EXEC;
		goto err;
	}

	/* 2. Mount old ploop */
	snprintf(from, sizeof(from), "%s/mnt", old_tmpdir);
	mkdir(from, 0700);
	rc = mount_ploop(old_tmpdir, from, opts_vztt);
	if (rc)
		goto err;
	old_mounted = 1;

	/* 3. Create new ploop */
	vztt_logger(1, 0, "Creating new format ploop...", old_cache, cache);
	rc = create_tmp_dir(&new_tmpdir);
	if (rc)
		goto err;
	/* Get the size from old ploop DiskDescriptor.xml */
	if ((rc = open_ploop_di(old_tmpdir, &di)))
		goto err;
	rc = create_ploop(new_tmpdir, di->size / 2, opts_vztt);
	if (rc)
		goto err;

	/* 4. Mount new ploop */
	snprintf(to, sizeof(to), "%s/mnt", new_tmpdir);
	mkdir(to, 0700);
	rc = mount_ploop(new_tmpdir, to, opts_vztt);
	if (rc)
		goto err;
	new_mounted = 1;

	/* 4. Copy */
	vztt_logger(1, 0, "Copying data...", old_cache, cache);
	if ((rc = copy_data(from, ".", to, opts_vztt)) ||
		(rc = copy_data(old_tmpdir, "templates", new_tmpdir, opts_vztt)))
		goto err;

	/* 5. Release space if possible */
	resize_ploop(new_tmpdir, opts_vztt, 0);

	/* 6. Umount destination ploop */
	umount_ploop(new_tmpdir, opts_vztt);
	new_mounted = 0;

	/* 7. Unlink mnt */
	rmdir(to);

	/* 8. Pack */
	rc = pack_ploop(new_tmpdir, cache, opts_vztt);

	/* 9. Remove old cache */
	if (rc == 0)
		unlink(old_cache);

	progress(PROGRESS_CREATE_PLOOP, 100, opts_vztt->progress_fd);

err:

	if (old_tmpdir) {
		if (old_mounted)
			umount_ploop(old_tmpdir, opts_vztt);
		remove_directory(old_tmpdir);
		free(old_tmpdir);
	}

	if (new_tmpdir) {
		if (new_mounted)
			umount_ploop(new_tmpdir, opts_vztt);
		remove_directory(new_tmpdir);
		free(new_tmpdir);
	}

	if (rc)
		vztt_logger(0, 0, "Failed to convert %s to ploop v2 format",
				old_cache);
	else
		vztt_logger(1, 0, "Done.", old_cache, cache);
	ploop_close_dd(di);

	return rc;
}

static unsigned long get_ploop_block_size(const char *ploop_env)
{
	unsigned long block_size = 0;
	char *ploop_block_size;
	char *endptr;

	ploop_block_size = getenv(ploop_env);

	if (ploop_block_size)
	{
		block_size = strtoul(ploop_block_size, &endptr, 0);
		if (*endptr != '\0')
		{
			block_size = 0;
			vztt_logger(0, 0, "Failed to use %s=%s ploop block size, " \
				"using defaults", ploop_env, ploop_block_size);
		}
	}

	return block_size;
}

int create_ploop(char *ploop_dir, unsigned long long diskspace_kb, struct options_vztt *opts_vztt)
{
	int rc = 0;
	char path[PATH_MAX+1];
	char fstype[] = "ext4";
	struct ploop_create_param param = {};

	progress(PROGRESS_CREATE_PLOOP, 0, opts_vztt->progress_fd);

	snprintf(path, sizeof(path), "%s/"IMAGE_NAME, ploop_dir);

	/* create ploop image */
	param.image = path;
	param.mode = PLOOP_EXPANDED_MODE;
	if (ploop_is_large_disk_supported())
		param.fmt_version = PLOOP_FMT_V2;
	else
		param.fmt_version = PLOOP_FMT_V1;
	param.size = (__u64) diskspace_kb * 2; // 512B blocks
	param.fstype = fstype;

	/* use custom cluster block size */
	param.blocksize = get_ploop_block_size("PLOOP_BLOCK_SIZE");

	/* use custom file system block size */
	param.fsblocksize = get_ploop_block_size("PLOOP_FSBLOCK_SIZE");

	/* allow to work on ext3 */
	putenv("PLOOP_SKIP_EXT4_EXTENTS_CHECK=yes");

	if ((rc = ploop_create_image(&param)))
	{
		vztt_logger(0, 0, "Failed to create ploop image %s: %s %i",
			path, ploop_get_last_error(), rc);
		rc = VZT_PLOOP_ERROR;
	}

	putenv("PLOOP_SKIP_EXT4_EXTENTS_CHECK");

	progress(PROGRESS_CREATE_PLOOP, 100, opts_vztt->progress_fd);
	return rc;
}

int mount_ploop(char *ploop_dir, char *to, struct options_vztt *opts_vztt)
{
	int rc = 0;
	char fstype[] = "ext4";
	char mount_data[] = "pfcache_csum";
	struct ploop_mount_param mountopts = {};
	struct ploop_disk_images_data *di = 0;

	mountopts.fstype = fstype;
	mountopts.mount_data = mount_data;
	mountopts.target = to;

	if ((rc = open_ploop_di(ploop_dir, &di)))
		goto cleanup_0;

	/* allow to work on ext3 */
	putenv("PLOOP_SKIP_EXT4_EXTENTS_CHECK=yes");

	/* Mount ploop */
	if ((rc = ploop_mount_image(di, &mountopts)))
	{
		vztt_logger(0, 0, "Failed to mount ploop image %s: %s %i",
			ploop_dir, ploop_get_last_error(), rc);
		rc = VZT_PLOOP_ERROR;
	}

	putenv("PLOOP_SKIP_EXT4_EXTENTS_CHECK");

cleanup_0:
	ploop_close_dd(di);

	return rc;
}

int umount_ploop(char *ploop_dir, struct options_vztt *opts_vztt)
{
	int rc = 0;
	struct ploop_disk_images_data *di = 0;

	if ((rc = open_ploop_di(ploop_dir, &di)))
		goto cleanup_0;

	/* Umount ploop */
	if ((rc = ploop_umount_image(di)))
	{
		vztt_logger(0, 0, "Failed to umount ploop image %s: %s %i",
			ploop_dir, ploop_get_last_error(), rc);
		rc = VZT_PLOOP_ERROR;
	}

cleanup_0:
	ploop_close_dd(di);

	return rc;
}

int pack_ploop(char *ploop_dir, char *to_file, struct options_vztt *opts_vztt)
{
	int rc = 0;
	char path[PATH_MAX+1];
	char cmd[2*PATH_MAX+1];
	char *pwd = NULL;

	if (getcwd(path, sizeof(path)))
		pwd = strdup(path);

	if (chdir(ploop_dir) == -1) {
		vztt_logger(0, errno, "chdir(%s) error", ploop_dir);
		return VZT_CANT_CHDIR;
	}
	get_pack_cmd(cmd, sizeof(cmd), to_file, ".", "--numeric-owner");

	if ((rc = exec_cmd(cmd, (opts_vztt->flags & OPT_VZTT_QUIET)))) {
		vztt_logger(0, errno, "system(%s) error", cmd);
		rc = VZT_CANT_EXEC;
	}
	if (pwd)
		if (chdir(pwd) == -1)
			vztt_logger(0, errno, "chdir(%s) error", ploop_dir);

	VZTT_FREE_STR(pwd);
	return rc;
}

int resize_ploop(char *ploop_dir, struct options_vztt *opts_vztt, unsigned long long size)
{
	int rc = 0;
	struct ploop_disk_images_data *di = 0;
	struct ploop_resize_param resize_param = {.size = 0, .offline_resize = 0};

	progress(PROGRESS_RESIZE_PLOOP, 0, opts_vztt->progress_fd);

	resize_param.size = size * 2; // 512B blocks

	if ((rc = open_ploop_di(ploop_dir, &di)))
		goto cleanup_0;

	if ((rc = ploop_resize_image(di, &resize_param)))
	{
		vztt_logger(0, 0, "Failed to resize ploop %s: %s %i",
			ploop_dir, ploop_get_last_error(), rc);
		rc = VZT_PLOOP_ERROR;
	}

cleanup_0:
	ploop_close_dd(di);
	progress(PROGRESS_RESIZE_PLOOP, 100, opts_vztt->progress_fd);

	return rc;
}

int create_ploop_dir(char *ve_private, char **ploop_dir) {
	// root.hdd hardcoded in vzctl
	if (((*ploop_dir) = malloc(strlen(ve_private) + 10)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	sprintf((*ploop_dir), "%s/root.hdd", ve_private);
	if (mkdir((*ploop_dir), 0755) != 0) {
		vztt_logger(0, 0, "Failed to create %s ploop dir", (*ploop_dir));
		return VZT_CANT_CREATE;
	}

	return 0;
}
