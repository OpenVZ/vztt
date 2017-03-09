/*
 * Copyright (c) 2015-2017, Parallels International GmbH
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
 * Our contact details: Parallels IP Holdings GmbH, Vordergasse 59, 8200
 * Schaffhausen, Switzerland.
 *
 * Chroot test module
 */

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

int main(int argc, char **argv, char **envp)
{
	int dir_fd;       /* File descriptor to directory */  
	struct stat sbuf; /* The stat() buffer */
	char *target_dir; 
	char *pythonpath;
	char **nargv;
	int i;
	char *bin;
    char **nenvp;

	/*
	PArameters are:
	target (chroot) directory
	PYTHONPATH environment variable
	program to run
	... options
	*/

	if ( argc < 4 )
    {
        fprintf(stderr, "Usage: target_dir PYTHONPATH progname ...\n");
        exit(1);
    }

	target_dir = argv[1];
	pythonpath = (char *)malloc(strlen(argv[2]) + 15);
	if ( NULL == pythonpath )
    {
        fprintf(stderr, "malloc error %s\n", strerror(errno));
        exit(1);
    }
	strcpy(pythonpath, "PYTHONPATH=");
	strcat(pythonpath, argv[2]);
	bin=argv[3];
	if ( NULL == (nargv=(char **)malloc(sizeof(char *)*(argc-2))) )
    {
        fprintf(stderr, "malloc error %s\n", strerror(errno));
        exit(1);
    }
	for (i=0;i<argc-3;i++) {
		nargv[i] = argv[i+3];
	}
	nargv[argc-3] = NULL;

	/* 
		To copy all environment variables and add PYTHONPATH.
		We should to copy VZCTL_ENV and other needs variables to chroot 
		and vzctl will copy this vars into VPS 
	*/
	for (i = 0; envp[i] != NULL; i++) ;
	if ( NULL == (nenvp = (char **)calloc(i+2, sizeof(char *))) )
    {
        fprintf(stderr, "malloc error %s\n", strerror(errno));
        exit(1);
    }
	for (i = 0; envp[i] != NULL; i++)
		nenvp[i] = envp[i];
	nenvp[i] = pythonpath;
	nenvp[i+1] = NULL;

	if (stat(target_dir,&sbuf) < 0) {
		fprintf(stderr,"Failed to stat %s - %s\n", target_dir, strerror(errno));
		exit(1);
	}
	else if (!S_ISDIR(sbuf.st_mode)) {
		fprintf(stderr,"Error - %s is not a directory!\n",target_dir);
		exit(1);
	}
   
	if ((dir_fd=open("/",O_RDONLY)) < 0)
	{
		fprintf(stderr,"Failed to open '.' for reading - %s\n",strerror(errno));
		exit(1);
	}

	/*  
	** Next we chroot() to the target directory
	*/
	if (chroot(target_dir)<0) {
		fprintf(stderr,"Failed to chroot to %s - %s\n",target_dir,
			strerror(errno));
		exit(1);
	}

	if (fchdir(dir_fd)<0) {
		fprintf(stderr,"Failed to fchdir - %s\n", strerror(errno));
		exit(1);
	}
	close(dir_fd);

	if (execve(bin,nargv,nenvp)<0) {
		fprintf(stderr,"Failed to exec %s - %s\n",bin,strerror(errno));
		exit(1);
	}

	return 0;
}
