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
 * File download
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <unistd.h>
#include <libgen.h>
#include <utime.h>
#include <sched.h>
#include <fcntl.h>

#include "downloader.h"
#include "vztt_error.h"
#include "vzcommon.h"
#include "util.h"

struct _clone_param {
	int fds[2];
	char *const *argv;
};

#define STACK_SIZE 2 * 4096

/* Clone the process */
static int run_clone(void *data)
{
	struct _clone_param *param = (struct _clone_param *)data;
	int fd;

	fd = open("/dev/null", O_RDWR);

	/* allow C-c for child */
	signal(SIGINT, SIG_DFL);
	/* allow sigquit for child */
	signal(SIGQUIT, SIG_DFL);

	close(param->fds[0]);

	dup2(fd, STDIN_FILENO);
	dup2(param->fds[1], STDERR_FILENO);
	dup2(param->fds[1], STDOUT_FILENO);

	close(param->fds[1]);

	execvp(param->argv[0], param->argv);
	vztt_logger(0, errno, "execve(%s...) failed", param->argv[0]);
	return VZT_CANT_EXEC;
}

/*
  To download <file> tp <dst>. Will use curl binary:
  curl --silent --output <path> --create-dirs --proxy <host[:port]>\
		 --proxy-user <user[:password]> <url>
*/
int fetch_file(const char *tmpdir, const char *file, struct _url *u, const char *dst, int debug)
{
	int rc = 0;
	int i;
	char *cmd = "curl";
	char *argv[100];
	char *proxy = NULL;
	char *proxy_user = NULL;

	struct sigaction act_chld, act_quit, act_int;
	pid_t chpid, pid;
	int status;
	void *stack;
	int flags = 0;
	int ndx = 0;
	struct _clone_param param;
	int sa_flags;

	FILE *fp;
	char buffer[BUFSIZ];

	argv[ndx++] = cmd;
	argv[ndx++] = "--silent";
	argv[ndx++] = "--show-error";
	argv[ndx++] = "--output";
	argv[ndx++] = (char *)dst;
	if (u && u->server && u->proto) {
		size_t size;
		size = strlen(u->proto) + strlen(u->server) + 20;
		if ((proxy = (char *)malloc(size)) == NULL)
		{
			vztt_logger(0, errno, "malloc() : %m");
			rc = VZT_CANT_ALLOC_MEM;
			goto cleanup_0;
		}
		if (u->port)
			snprintf(proxy, size, "%s://%s:%s", u->proto, u->server, u->port);
		else
			snprintf(proxy, size, "%s://%s", u->proto, u->server);
		argv[ndx++] = "--proxy";
		argv[ndx++] = proxy;
		size = strlen(u->proto) + strlen(u->server) + 20;
		if (u->user) {
			size = strlen(u->user) + 3;
			if (u->passwd)
				size += strlen(u->passwd);
			if ((proxy_user = (char *)malloc(size)) == NULL) {
				vztt_logger(0, errno, "malloc() : %m");
				rc = VZT_CANT_ALLOC_MEM;
				goto cleanup_0;
			}
			if (u->passwd)
				snprintf(proxy_user, size, "%s:%s", u->user, u->passwd);
			else
				strncpy(proxy_user, u->user, size);
			argv[ndx++] = "--proxy_user";
			argv[ndx++] = proxy_user;
		}
	}
	argv[ndx++] = (char *)file;
	argv[ndx] = NULL;
	param.argv = (char *const *)argv;

	if (debug >= 4) {
		vztt_logger(2, 0, "Run %s with parameters:", argv[0]);
		for (i = 1; argv[i]; ++i)
			vztt_logger(2, 0, "\t%s", argv[i]);
	}

	/* Allocate stack */
	stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_GROWSDOWN, 0, 0);
	if (stack == MAP_FAILED) {
		vztt_logger(0, 0, "Can't allocate stack");
		rc = VZT_INTERNAL;
		goto cleanup_0;
	}

	flags = SIGCHLD;

	/* set default sigchld handler */
	sigaction(SIGCHLD, NULL, &act_chld);
	sa_flags = act_chld.sa_flags;
	act_chld.sa_flags = SA_NOCLDSTOP;
	sigaction(SIGCHLD, &act_chld, NULL);

	/* ignore sigquit & sigint in parent */
	sigaction(SIGQUIT, NULL, &act_quit);
	signal(SIGQUIT, SIG_IGN);
	sigaction(SIGINT, NULL, &act_int);
	signal(SIGINT, SIG_IGN);

	if (pipe(param.fds) < 0) {
		vztt_logger(0, errno, "pipe() error");
		rc = VZT_CANT_OPEN;
		goto cleanup_1;
	}

	if ((fp = fdopen(param.fds[0], "r")) == NULL) {
		close(param.fds[1]);
		vztt_logger(0, errno, "fdopen() error");
		rc = VZT_CANT_OPEN;
		goto cleanup_2;
	}

	if ((chpid = clone(run_clone, stack + STACK_SIZE, flags, (void *)&param)) < 0) {
		close(param.fds[1]);
		vztt_logger(0, errno, "clone() failed");
		rc = VZT_CANT_FORK;
		goto cleanup_3;
	}
	close(param.fds[1]);

	while(fgets(buffer, sizeof(buffer), fp))
		vztt_logger(0, 0, "%s : %s", param.argv[0], buffer);

	while ((pid = waitpid(chpid, &status, 0)) == -1)
		if (errno != EINTR)
			break;

	if (pid == chpid) {
		if (WIFEXITED(status)) {
			int retcode;
			if ((retcode = WEXITSTATUS(status))) {
				vztt_logger(0, 0, "%s failed, exitcode=%d", argv[0], retcode);
				rc = VZT_PM_FAILED;
			} else {
				rc = 0;
			}
		} else if (WIFSIGNALED(status)) {
			vztt_logger(0, 0,  "Got signal %d", WTERMSIG(status));
			rc = VZT_PROG_SIGNALED;
		}
	} else if (pid < 0) {
		vztt_logger(0, errno, "Error in waitpid()");
		rc = VZT_INTERNAL;
	}

cleanup_3:
	fclose(fp);
cleanup_2:
	close(param.fds[0]);
cleanup_1:
	sigaction(SIGINT, &act_int, NULL);
	sigaction(SIGQUIT, &act_quit, NULL);
	act_chld.sa_flags = sa_flags;
	sigaction(SIGCHLD, &act_chld, NULL);
	munmap(stack, STACK_SIZE);
cleanup_0:
	if (proxy)
		free(proxy);
	if (proxy_user)
		free(proxy_user);
	return rc;
}
