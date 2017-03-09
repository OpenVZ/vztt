/*
 * Copyright (c) 2010-2017, Parallels International GmbH
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
 * Wrapper to set kernel osrelease (optionally) + chroot + exec
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#define __USE_GNU
#include <sched.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "run_from_chroot.h"

#ifndef CLONE_NEWUTS
#define CLONE_NEWUTS 0x04000000
#endif

struct clone_params {
	const char *cmd;
	const char *envdir;
	const char **argv;
	char **envp;
	char *virt_osrelease;
};

static
void my_logger(
		int err,
		const char* fmt,
		...
	)
{

	va_list ap;
	char *buff;

	buff = malloc(LOG_BUFF_SZ);
	if (!buff)
		return;

	va_start(ap, fmt);
	vsnprintf(buff, LOG_BUFF_SZ, fmt, ap);
	fprintf(stderr, "Errno: %i, message: %s\n", err, buff);
	va_end(ap);

}

static int run_clone(void *data)
{
	struct clone_params *params;
	int dir_fd;
	int virt_osrelease_fd;

	params = data;

	/* Apply the virt_osrelease hack */
	if (params->virt_osrelease)
	{
		if ((virt_osrelease_fd = open("/proc/sys/kernel/virt_osrelease",
			O_RDWR | O_TRUNC)) < 0)
			return RUN_FROM_CHROOT_CANT_OPEN;
		if ((write(virt_osrelease_fd, params->virt_osrelease,
			strlen(params->virt_osrelease))) <= 0)
		{
			close(virt_osrelease_fd);
			return RUN_FROM_CHROOT_CANT_WRITE;
		}
		close(virt_osrelease_fd);
	}

	/* Cd to / to make availability to unjump from chroot */
	if ((dir_fd=open("/", O_RDONLY)) < 0) {
		my_logger(errno, "Can not open / directory");
		return RUN_FROM_CHROOT_CANT_OPEN;
	}

	/* Next we chroot() to the target directory */
	if (chroot(params->envdir) < 0) {
		my_logger(errno, "chroot(%s) failed", params->envdir);
		return RUN_FROM_CHROOT_CANT_CHROOT;
	}

	/* Go back */
	if (fchdir(dir_fd) < 0) {
		my_logger(errno, "fchdir(%s) failed", params->envdir);
		close(dir_fd);
		return RUN_FROM_CHROOT_CANT_CHDIR;
	}
	close(dir_fd);

	/* allow C-c for child */
	signal(SIGINT, SIG_DFL);
	/* allow sigquit for child */
	signal(SIGQUIT, SIG_DFL);
	execve(params->cmd, (char **) params->argv, params->envp);
	my_logger(errno, "execve(%s...) failed", params->cmd);
	return RUN_FROM_CHROOT_CANT_EXEC;
}

/* run <cmd> from chroot environment <envdir> with arguments <argv> 
   and environments <envp> */
static int run_from_chroot(
		const char *cmd,
		const char *envdir,
		const char **argv,
		char **envp)
{
	pid_t chpid, pid;
	int status;
	int rc = 0;
	struct stat st;
	struct sigaction act, act_chld, act_quit, act_int;
	struct clone_params params;
	int flags = SIGCHLD;
	void *stack;

	/* Allocate stack */
	stack = mmap(NULL, 2 * 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE |
		MAP_ANON | MAP_GROWSDOWN, 0, 0);
	if (stack == MAP_FAILED) {
		my_logger(0, "Can't allocate stack");
		return RUN_FROM_CHROOT_INTERNAL;
        }

	/* environment directory checking */
	if (!envdir) {
		my_logger(0, "Environment directory is not defined");
		return RUN_FROM_CHROOT_INTERNAL;
	}
	if (stat(envdir, &st)) {
		my_logger(0, "stat(%s) error", envdir);
		return RUN_FROM_CHROOT_INTERNAL;
	}
	if (!S_ISDIR(st.st_mode)) {
		my_logger(0, "%s exist, but is not directory", envdir);
		return RUN_FROM_CHROOT_INTERNAL;
	}

	/* set default sigchld handler */
	sigaction(SIGCHLD, NULL, &act_chld);
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_NOCLDSTOP;
	act.sa_handler = SIG_DFL;
	sigaction(SIGCHLD, &act, NULL);

	/* ignore sigquit & sigint in parent */
	sigaction(SIGQUIT, NULL, &act_quit);
	signal(SIGQUIT, SIG_IGN);
	sigaction(SIGINT, NULL, &act_int);
	signal(SIGINT, SIG_IGN);

	params.cmd = cmd;
	params.envdir = envdir;
	params.argv = argv;
	params.envp = envp;
	params.virt_osrelease = getenv("VIRT_OSRELEASE");

	if (params.virt_osrelease)
		flags |= CLONE_NEWUTS;

	if ((chpid = clone(run_clone, stack + 2 * 4096, flags,
		(void *) &params)) < 0) {
		my_logger(errno, "clone() failed");
		return RUN_FROM_CHROOT_CANT_FORK;
	}

	while ((pid = waitpid(chpid, &status, 0)) == -1)
		if (errno != EINTR)
			break;

	if (pid < 0) {
		my_logger(errno, "Error in waitpid()");
		rc = RUN_FROM_CHROOT_INTERNAL;
		goto cleanup;
	}
	if (WIFEXITED(status)) {
		int retcode;
		if ((retcode = WEXITSTATUS(status))) {
			my_logger(0, 
				"%s failed, exitcode=%d",
				cmd, retcode);
			rc = RUN_FROM_CHROOT_CMD_FAILED;
		}
	}
	else if (WIFSIGNALED(status)) {
		my_logger(0,  "Got signal %d", WTERMSIG(status));
		rc = RUN_FROM_CHROOT_PROG_SIGNALED;
	}

cleanup:
	sigaction(SIGINT, &act_int, NULL);
	sigaction(SIGQUIT, &act_quit, NULL);
	sigaction(SIGCHLD, &act_chld, NULL);

	return rc;
}

int main(int argc, const char *argv[]) {
	int rc = 1;
	char *envs[2];

	if (geteuid() != 0) { 
		my_logger(0, "%s: must be superuser\n", argv[0]); 
		return rc;
	} 


	if (argc < 3) {
		my_logger(0, "Bad parameters\n");
		return rc;
	}

	envs[0] = malloc(LOG_BUFF_SZ);
	if (!envs[0]){
		my_logger(errno, "Malloc failed\n");
		return RUN_FROM_CHROOT_INTERNAL;
	}

	sprintf(envs[0], "%s", "PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin:/root/bin");
	envs[1] = NULL;

	/* run cmd from chroot environment */
	if ((rc = run_from_chroot(argv[2], argv[1], argv+2, envs)))
		return rc;

	if(envs[0])
		free(envs[0]);

	return 0;
}
