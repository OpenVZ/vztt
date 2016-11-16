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
 * template set lock module
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
#include <time.h>
#include <poll.h>
#include <signal.h>
#include <vzctl/libvzctl.h>
#include <libgen.h>

#include "vztt_error.h"
#include "util.h"
#include "template.h"
#include "lock.h"

#include <vzctl/libvzctl.h>

/*
vzpkg write lock operations:
 update metadata - done (change on onread ?)
 clean - done
 remove template - done
 upgrade area - done (change on onread ?)

vzpkg read lock operations:
 create cache - done
 update cache - done
 remove cache - done
 install - done
 update - done
 remove - done
 localinstall - done
 localupdate - done
 list - done
 repair - done
 upgrade - done
 status - done
 link - done
 fetch - done
 info - done
 vzveconvert - done

vzpkg non lock operations:
 install template
 update template
 verify area
 get_backup_apps

*/

#define LOCK_MECH_NONE 0
#define LOCK_MECH_LCK  1

static int lock_mech = LOCK_MECH_NONE;
static time_t default_timeout = 600;

/************************************
  fcntl() template lock module
************************************/
static void alarm_handler(int sig)
{
	return;
}

static int file_lock(
		const char *path,
		int mode,
		void **lockdata,
		int timeout)
{
	int rc;
	int fd;
	struct flock fl;
	struct sigaction act;
	struct sigaction old_act;

	if ((*lockdata = (void *)malloc(sizeof(fd))) == NULL)
		return vztt_error(VZT_SYSTEM, errno, "malloc() :");

	if (mode == LOCK_WRITE)
		fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0600);
	else
		fd = open(path, O_RDONLY|O_CREAT, 0600);
	if (fd < 0) {
		free((void *)*lockdata);
		*lockdata = NULL;
		return vztt_error(VZT_SYSTEM, errno, "open(%s) :", path);
	}
	fcntl(fd, F_SETFD, FD_CLOEXEC);
	fl.l_type = (mode == LOCK_WRITE) ? F_WRLCK : F_RDLCK;
	fl.l_start=0;
	fl.l_whence = SEEK_SET;
	fl.l_len = 0; /* until EOF */

	if (timeout) {
		act.sa_flags = 0;
		sigemptyset(&act.sa_mask);
		act.sa_handler = alarm_handler;
		sigaction(SIGALRM, &act, &old_act);
		alarm(timeout);
	}

	rc = fcntl(fd, timeout ? F_SETLKW : F_SETLK, &fl);

	if (timeout) {
		alarm(0);
		sigaction(SIGALRM, &old_act, NULL);
	}

	if (rc) {
		if (errno == EINTR) {
			rc = vztt_error(VZT_CANT_LOCK, 0, 
				"lock timeout exceeded");
		} else if (errno == EACCES || errno == EAGAIN) {
			rc = vztt_error(VZT_CANT_LOCK, 0,
				"template cache file locked");
		} else {
			rc = vztt_error(VZT_SYSTEM, errno, 
				"fcntl(%s, F_SETLKW, ...) :", path);
		}
		free(*lockdata);
		*lockdata = NULL;
		close(fd);
		return rc;
	}
	memcpy(*lockdata, &fd, sizeof(fd));
	vztt_logger(2, 0, "lock file %s locked", path);
	return 0;
}

static int file_unlock(void *lockdata)
{
	int fd = *((int *)lockdata);
	close(fd);
	vztt_logger(2, 0, "lock file unlocked");
	return 0;
}

/************************************
  Common template lock module
************************************/

static int lock_init(const char *tmpldir)
{
	lock_mech = LOCK_MECH_LCK;
	return 0;
}

/* lock base os template */
int tmpl_lock(
		struct global_config *gc,
		struct base_os_tmpl *tmpl,
		int mode,
		int vztt,
		void **lockdata)
{
	int rc;
	char lock[PATH_MAX + 1];

	if (vztt & OPT_VZTT_SKIP_LOCK)
		return 0;

	if (lock_mech == LOCK_MECH_NONE) {
		/* locking not initialized yet */
		if ((rc = lock_init(gc->template_dir)))
			return rc;
	}

	if (lock_mech == LOCK_MECH_LCK) {
		snprintf(lock, sizeof(lock) - 1, "%s/.lock", tmpl->basedir);
		rc = file_lock(lock, mode, lockdata, default_timeout);
	} else {
		return vztt_error(VZT_INTERNAL, 0, "Undefined lock type in tmpl_lock()");
	}

	if (rc == 0)
		vztt_logger(2, 0, "template %s locked", tmpl->name);
	return rc;
}

int cache_lock(
		struct global_config *gc,
		const char *cache_path,
		int mode,
		int vztt,
		void **lockdata)
{
	int rc;
	char lock[PATH_MAX + 1];

	if (vztt & OPT_VZTT_SKIP_LOCK)
		return 0;

	if (lock_mech == LOCK_MECH_NONE) {
		/* locking not initialized yet */
		if ((rc = lock_init(gc->template_dir)))
			return rc;
	}

	if (lock_mech == LOCK_MECH_LCK) {
		snprintf(lock, sizeof(lock) - 1, "%s.lock", cache_path);
		rc = file_lock(lock, mode, lockdata, 0);
	} else {
		return vztt_error(VZT_INTERNAL, 0, "Undefined lock type in cache_lock()");
	}

	if (rc == 0)
		vztt_logger(2, 0, "cache %s locked", cache_path);
	return rc;
}

static int do_unlock(void *lockdata, int vztt, int timeout)
{
	int rc = 0;

	if (vztt & OPT_VZTT_SKIP_LOCK)
		return 0;

	if (lock_mech == LOCK_MECH_LCK) {
		rc = file_unlock(lockdata);
	} else {
		return vztt_error(VZT_INTERNAL, 0, 
			"Undefined lock type in do_unlock()");
	}
	free(lockdata);

	return rc;
}

/* unlock base os template */
int tmpl_unlock(void *lockdata, int vztt)
{
	int rc = do_unlock(lockdata, vztt, default_timeout);
	if (rc == 0)
		vztt_logger(2, 0, "template unlocked");
	return rc;
}

int cache_unlock(void *lockdata, int vztt)
{
	int rc = do_unlock(lockdata, vztt, 0);
	if (rc == 0)
		vztt_logger(2, 0, "template cache unlocked");
	return rc;
}

int lock_ve(const char *ctid, int vztt, void **lockdata)
{
	const char *status;
	int fd, err;
	struct vzctl_env_handle *h;

	if (vztt & OPT_VZTT_SKIP_LOCK)
		return 0;

	status = (vztt & OPT_VZTT_TEST) ? "check-updating" : "updating";
	h = vzctl2_env_open(ctid, VZCTL_CONF_SKIP_NON_EXISTS, &err);
	if (h == NULL) {
		vztt_logger(0, 0, "vzctl2_env_open %s: %s",
				ctid, vzctl2_get_last_error());
		return VZT_CANT_LOCK;
	}

	fd = vzctl2_env_lock(h, status);
	vzctl2_env_close(h);
	if ((fd == -1) || (fd == -2)) {
		if (fd == -2)
			vztt_logger(0, 0, 
				"Can not lock container %s: locked", ctid);
		else
			vztt_logger(0, 0, "Can not lock container %s", ctid);
		return VZT_CANT_LOCK;
	}
	memcpy(lockdata, &fd, sizeof(fd));
	vztt_logger(2, 0, "Container %u successfully locked, fd=%d", ctid, fd);

	return 0;
}

void unlock_ve(const char *ctid, void *lockdata, int vztt)
{
	int fd, err;
	struct vzctl_env_handle *h;

	if (vztt & OPT_VZTT_SKIP_LOCK)
		return;
	memcpy(&fd, &lockdata, sizeof(fd));
	h = vzctl2_env_open(ctid, VZCTL_CONF_SKIP_NON_EXISTS, &err);
	if (h == NULL) {
		vztt_logger(0, 0, "vzctl2_env_open %s: %s",
				ctid, vzctl2_get_last_error());
		vzctl2_env_unlock(NULL, fd);
	} else {
		vzctl2_env_unlock(h, fd);
		vzctl2_env_close(h);
	}
	vztt_logger(2, 0, "Container %s unlocked, fd=%d", ctid, fd);
}

/* lock ve, create and lock config */
static int lock_veid(const char *ctid, int vztt, void **lockdata)
{
	char path[PATH_MAX+1];
	int rc;
	vzctl_env_status_t ve_status;

	/* get VE status */
	if (vzctl2_get_env_status(ctid, &ve_status, ENV_STATUS_ALL))
		return 1;
	if (ve_status.mask & ENV_STATUS_EXISTS)
		return 1;
	/* ve may be deleted but running */
	if (ve_status.mask & ENV_STATUS_RUNNING)
		return 1;

	snprintf(path, sizeof(path), ENV_CONF_DIR "%s.conf", ctid);
	if (access(path, F_OK) == 0)
		return 1;

	/* Note: VE config does not exist yet and
	   vzctl will lock VE in local mode only */
	if ((rc = lock_ve(ctid, vztt, lockdata)))
		return rc;

        return 0;
}

/* Find and lock nearest veid */
int lock_free_veid(int vztt, ctid_t ctid, void **lockdata)
{
	/*
	 Ugly hack to try first VE #1, then #50 as temporary one -
	 since it works with expired license
	*/
	vzctl2_generate_ctid(ctid);	
	if (lock_veid(ctid, vztt, lockdata) == 0)
		return 0;

	vztt_logger(0, 0, "Can not lock free VEID");
	return VZT_CANT_LOCK_FREE_VE;
}
