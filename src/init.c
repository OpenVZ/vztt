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
 * Init -- very simple init stub
 */

#define MOUNT_PROC 1
#define IF_LOOP	1
#define LOCALHOST_ENV 1

#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>

#ifdef IF_LOOP
#include <sys/socket.h>
#include <linux/if.h>
#include <string.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#endif

#ifdef MOUNT_PROC
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
/* Taken from /usr/src/linux/include/fs.h */
#ifndef MS_POSIXACL
#define MS_POSIXACL	(1<<16) /* VFS does not apply the umask */
#endif
#ifndef MS_ACTIVE
#define MS_ACTIVE	(1<<30)
#endif
#ifndef MS_NOUSER
#define MS_NOUSER	(1<<31)
#endif
#endif

/* Set a signal handler */
static void setsig(struct sigaction *sa, int sig, 
		   void (*fun)(int), int flags)
{
	sa->sa_handler = fun;
	sa->sa_flags = flags;
	sigemptyset(&sa->sa_mask);
	sigaction(sig, sa, NULL);
}

/*
 * SIGCHLD: one of our children has died.
 */
void chld_handler()
{
	int st;

	/* R.I.P. all children */
	while((waitpid(-1, &st, WNOHANG)) > 0)
		;
}


/*
 * The main loop
 */ 
int main(int argc, char * argv[])
{
	struct sigaction sa;
	int i;

	if (geteuid() != 0) {
		fprintf(stderr, "%s: must be superuser\n", argv[0]);
		exit(1);
	}

	if (getpid() != 1) {
		fprintf(stderr, "%s: must be a process with PID=1\n", argv[0]);
		exit(1);
	}

#ifdef MOUNT_PROC
	mkdir("/proc", 0555);
	mount("proc", "/proc", "proc",
		MS_POSIXACL|MS_ACTIVE|MS_NOUSER|0xec0000, 0);
#endif
#ifdef IF_LOOP
	/* to run loopback interface (ubuntu-7.04 samba requires, #91523) */
	int sd;
	struct ifreq ifr;
	struct sockaddr_in addr;

	if ((sd = socket(PF_INET, SOCK_DGRAM, 0)) >= 0) {
		memset(&ifr, 0, sizeof(struct ifreq));
		strncpy(ifr.ifr_name, "lo", IFNAMSIZ);
		ifr.ifr_flags = IFF_UP|IFF_LOOPBACK|IFF_RUNNING;
		if (ioctl(sd, SIOCSIFFLAGS, &ifr) >= 0) {
			strncpy(ifr.ifr_name, "lo", IFNAMSIZ);
			addr.sin_family = AF_INET;
			addr.sin_addr.s_addr = inet_addr("127.0.0.1");
			memcpy((char *)&ifr.ifr_addr, (char *)&addr, 
				sizeof(struct sockaddr_in));
			if (ioctl(sd, SIOCSIFADDR, &ifr) < 0)
				fprintf(stderr, "ioctl(SIOCSIFADDR) : %s\n",
					strerror(errno));
			/* valid mask already set after address setting */
		} else {
			fprintf(stderr, "ioctl(SIOCSIFFLAGS) : %s\n",
				strerror(errno));
		}
		close(sd);
	}
#endif
#ifdef LOCALHOST_ENV
	const char *name = "localhost.localdomain";
	if (sethostname(name, strlen(name)) != 0) {
		fprintf(stderr, "cannot set hostname to %s\n", name);
	}
#endif

	/* Ignore all signals */
	for(i = 1; i <= NSIG; i++)
		setsig(&sa, i, SIG_IGN, SA_RESTART);

	setsig(&sa, SIGCHLD, chld_handler, SA_RESTART);

	close(0);
	close(1);
	close(2);
  	setsid();


	for(;;)
		pause();
}
