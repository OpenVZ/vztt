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
 * Package manager module
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <dirent.h>
#include <error.h>
#include <time.h>
#include <limits.h>
#include <signal.h>
#include <getopt.h>
#include <ctype.h>
#include <assert.h>
#include <ctype.h>
#include <vzctl/libvzctl.h>

#include "vzcommon.h"
#include "config.h"
#include "vztt_error.h"
#include "tmplset.h"
#include "transaction.h"
#include "apt.h"
#include "yum.h"
#include "zypper.h"
#include "util.h"

int find_tmp_dir(char **tmp_dir)
{
	struct stat st;
	int i;
	char *tdir = NULL;
	char *tmp_dirs[] = {VZ_TMP_DIR, "/var/tmp/", "/tmp/", NULL};
	/* check tmpdir */
	for (i = 0; tmp_dirs[i]; i++) {
		if (stat(tmp_dirs[i], &st))
			continue;
		if (!S_ISDIR(st.st_mode))
			continue;
		tdir = tmp_dirs[i];
		break;
	}
	if (tdir == NULL) {
		vztt_logger(0, 0, "Can not find temporary directory");
		return VZT_TMPDIR_NFOUND;
	}

	if ((*tmp_dir = strdup(tdir)) == NULL)
	{
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	return 0;
}

int create_tmp_dir(char **tmp_dir)
{
	char path[PATH_MAX+1];
	char *tdir = NULL;
	int rc = 0;

	if ((rc = find_tmp_dir(&tdir)))
	{
		return rc;
	}

	/* create temporary directory */
	snprintf(path, sizeof(path), "%s/vzpkg.XXXXXX", tdir);
	free(tdir);
	if (NULL == mkdtemp(path)) {
		vztt_logger(0, errno, "mkdtemp(%s) error", path);
		return VZT_CANT_CREATE;
	}
	chmod(path, S_IRUSR|S_IWUSR|S_IXUSR);
	if ((*tmp_dir = strdup(path)) == NULL)
	{
		vztt_logger(0, errno, "Cannot alloc memory");
		rmdir(path);
		return VZT_CANT_ALLOC_MEM;
	}

	return 0;
}

/* create package manager object and initialize it */
int pm_init_wo_vzup2date(
	const char *ctid,
	struct global_config *gc,
	struct vztt_config *tc,
	struct tmpl_set *tmpl,
	struct options_vztt *opts_vztt,
	struct Transaction **obj)
{
	int rc;
	char path[PATH_MAX+1];
	struct stat st;
	struct Transaction *pm;

	if (PACKAGE_MANAGER_IS_RPM_ZYPP(tmpl->base->package_manager)) {
		if ((rc = zypper_create(obj)))
			return rc;
	} else if (PACKAGE_MANAGER_IS_RPM(tmpl->base->package_manager)) {
		if ((rc = yum_create(obj)))
			return rc;
	} else if (PACKAGE_MANAGER_IS_DPKG(tmpl->base->package_manager)) {
		if ((rc = apt_create(obj)))
			return rc;
	} else {
		vztt_logger(0, 0, "Unknown package manager: %s", \
				tmpl->base->package_manager);
		return VZT_UNKNOWN_PACKAGE_MANAGER;
	}

	pm = *obj;

	if( (pm->logfile = strdup(VZPKGLOG)) == NULL )
	{   /* Since in another place it is strdup */
			vztt_logger(0, errno, "Cannot alloc memory");
			return VZT_CANT_ALLOC_MEM;
	}

	SET_CTID(pm->ctid, ctid);

	/* get pkgenv dir */
	snprintf(path, sizeof(path), VZ_PKGENV_DIR "%s", \
			tmpl->base->package_manager);

	if ((pm->envdir = strdup(path)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	/* get packages (rpms) architecture */
	if ((pm->pkgarch = pm->pm_os2pkgarch(tmpl->base->osarch)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	pm->tdata = tmpl;
	pm->pkgman = tmpl->base->package_manager;
	pm->tmpldir = gc->template_dir;
	pm->basedir = tmpl->base->basedir;
	pm->basesubdir = tmpl->base->basesubdir;

	/* get the maximum kernel osrelease from template and vzpkgenv */
	snprintf(path, sizeof(path), "%s/osrelease", pm->envdir);
	if (access(path, F_OK) == 0) {
		char *vzpkgenv_osrelease;
		// Read vzpkgenv osrelease
		if ((rc = read_string(path, &vzpkgenv_osrelease))) {
			vztt_logger(0, 0, "Failed to read vzpkgenv osrelease");
			return VZT_ENVDIR_BROKEN;
		}

		// Compare osreleases and use newer kernel
		if (compare_osrelease(tmpl->base->osrelease, vzpkgenv_osrelease) > 0)
			pm->osrelease = vzpkgenv_osrelease;
		else
			VZTT_FREE_STR(vzpkgenv_osrelease);
	}
	if (tmpl->base->osrelease && pm->osrelease == 0 &&
		(pm->osrelease = strdup(tmpl->base->osrelease)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	/* get repositories & mirrorlists for marked templates */
	repo_list_init(&pm->repositories);
 	repo_list_init(&pm->zypp_repositories);
	repo_list_init(&pm->mirrorlists);
	if ((rc = tmplset_get_urls(tmpl, &pm->repositories,
		&pm->zypp_repositories, &pm->mirrorlists))) {
		vztt_logger(0, 0, "No any usable repository or mirrorlist");
		return rc;
	}
	/* copy environments from td */
//	AddEnvs(td->environment);

	/* load proxy settings */
	if ((rc = get_proxy(gc, tc,
			&pm->http_proxy, &pm->ftp_proxy, &pm->https_proxy)))
		return rc;

	/* check & create local cache dir */
	snprintf(path, sizeof(path), "%s/%s/%s", \
			pm->tmpldir, pm->basesubdir, pm->datadir);
	if (access(path, F_OK)) {
		if (mkdir(path, 0755)) {
			vztt_logger(0, errno, "Can not create %s directory", \
				path);
			return VZT_CANT_CREATE;
		}
	}

	/* set options */
	pm->debug = opts_vztt->debug;
	pm->test = (opts_vztt->flags & OPT_VZTT_TEST);
	pm->force = (opts_vztt->flags & OPT_VZTT_FORCE);
	pm->depends = (opts_vztt->flags & OPT_VZTT_DEPENDS);
	pm->quiet = (opts_vztt->flags & OPT_VZTT_QUIET);
	pm->data_source = opts_vztt->data_source;
	pm->expanded = (opts_vztt->flags & OPT_VZTT_EXPANDED);
	pm->metadata_expire = tc->metadata_expire;
	pm->force_openat = (opts_vztt->flags & OPT_VZTT_FORCE_OPENAT);
	pm->interactive = (opts_vztt->flags & OPT_VZTT_INTERACTIVE);
	if (opts_vztt->logfile) {
		if ((pm->logfile = strdup(opts_vztt->logfile)) == NULL) {
			vztt_logger(0, errno, "Cannot alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	}

	if (tc->vztt_proxy) {
		if ((pm->vzttproxy = strdup(tc->vztt_proxy)) == NULL) {
			vztt_logger(0, errno, "Cannot alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	}

	/* copy url_map from vztt_config */
	pm->url_map = &tc->url_map;

	/* pass progress fd to pm */
	pm->progress_fd = opts_vztt->progress_fd;

	string_list_init(&pm->options);
	string_list_init(&pm->exclude);

	pm->pm_init(pm);

	/* envdir check moved to last operation:
	for some cases (template removing, as sample) environment is not needs
	at really and this error will be ignored with force option */
	if (stat(pm->envdir, &st)) {
		vztt_logger(0, errno, "Can't find environment directory %s", \
			pm->envdir);
		return VZT_ENVDIR_NFOUND;
	}
	if (!S_ISDIR(st.st_mode)) {
		vztt_logger(0, 0, "%s exist, but is not directory", pm->envdir);
		return VZT_ENVDIR_NFOUND;
	}

	rc = create_tmp_dir(&(pm->tmpdir));

	return rc;
}

/* create package manager object and initialize it */
int pm_init(
	const char *ctid,
	struct global_config *gc,
	struct vztt_config *tc,
	struct tmpl_set *tmpl,
	struct options_vztt *opts_vztt,
	struct Transaction **obj)
{
	int rc;
	char cmd[PATH_MAX+1];
	char path[PATH_MAX+1];
	char *pkgenv_suffix = NULL;
	struct stat st;

	if (opts_vztt->flags & OPT_VZTT_USE_VZUP2DATE) {

		/* get pkgenv dir */
		snprintf(path, sizeof(path), VZ_PKGENV_DIR "%s", \
				tmpl->base->package_manager);

		if ((stat(path, &st)) || (!S_ISDIR(st.st_mode))) {
			if (PACKAGE_MANAGER_IS_RPM(tmpl->base->package_manager)) {
				snprintf(cmd, sizeof(cmd), "vzpkgenv%s",
					tmpl->base->package_manager + strlen(RPM));
				if (strlen(tmpl->base->osarch) == 3) {
					pkgenv_suffix = strstr(cmd, "x86");
					if (pkgenv_suffix)
						*pkgenv_suffix = 0;
				}
			} else {
				snprintf(cmd, sizeof(cmd), "vzpkgenvdeb%s", \
					tmpl->base->package_manager + strlen(DPKG));
			}

			vztt_logger(1, 0, "Environment %s is not" \
			    " found, running " YUM \
			    " to install it...", path);
			if ((rc = yum_install_execv_cmd(cmd, (opts_vztt->flags & OPT_VZTT_QUIET), 1)))
			{
				vztt_logger(0, 0, "Failed to install the environment " \
					"package required for the template.");
				return rc;
			}
		}
	}

	return pm_init_wo_vzup2date(ctid, gc, tc, tmpl, opts_vztt, obj);
}

/* clean and remove temporary directory */
int pm_clean(struct Transaction *pm)
{
	int rc;

	pm->pm_clean(pm);

	string_list_clean(&pm->options);
	string_list_clean(&pm->exclude);

	repo_list_clean(&pm->repositories);
	repo_list_clean(&pm->zypp_repositories);
	repo_list_clean(&pm->mirrorlists);

	if (pm->tmpdir) {
		if ((rc = remove_directory(pm->tmpdir))) {
			vztt_logger(0, 0, "Can not remove %s directory", pm->tmpdir);
			return rc;
		}
	}

	VZTT_FREE_STR(pm->tmpdir);
	VZTT_FREE_STR(pm->outfile);
	VZTT_FREE_STR(pm->rootdir);
	VZTT_FREE_STR(pm->envdir);
	/* pm->pkgman is a copy of pm->tdata->base->package_manager */
	VZTT_FREE_STR(pm->pkgarch);
	/* pm->tmpldir is a copy of gc->template_dir */
	/* pm->basedir is a copy of pm->tdata->base->basedir */
	/* pm->basesubdir - the same */
	clean_url(&pm->http_proxy);
	clean_url(&pm->ftp_proxy);
	clean_url(&pm->https_proxy);
	VZTT_FREE_STR(pm->vzttproxy);
	/* pm->datadir points to constant string */
	/* pm->pm_type points to constant string */
	VZTT_FREE_STR(pm->logfile);
	/* malloc allocated either YumTransaction (yum.c:yum_create:123)
	 * or AptTransaction (apt.c:apt_create:120) */
	VZTT_FREE_STR(pm->osrelease);
	free(pm);

	return 0;
}

//static struct sigaction act_int;
/*
void transaction_cleanup(int code)
{
	chroot(".");
//	(*act_int.sa_handler)(code);
}
*/

/* create proxy environment variable and add into list <envs> */
int add_proxy_env(
		const struct _url *proxy,
		const  char *var,
		struct string_list *envs)
{
	int rc = 0;
	size_t size;
	char *str;

	if (proxy == NULL)
		return 0;
	if (proxy->server == NULL)
		return 0;

	/* calculate proxy string size and alloc it */
	size = strlen(var) + strlen(proxy->server) + strlen("=://:@:") + 1;
	if (proxy->proto)
		size += strlen(proxy->proto);
	if (proxy->user)
		size += strlen(proxy->user);
	if (proxy->passwd)
		size += strlen(proxy->passwd);
	if (proxy->port)
		size += strlen(proxy->port);

	if ((str = (char *)malloc(size)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory\n");
		return VZT_CANT_ALLOC_MEM;
	}
	strncpy(str, var, size);
	strncat(str, "=", size-strlen(str)-1);
	if (proxy->proto) {
		strncat(str, proxy->proto, size-strlen(str)-1);
		strncat(str, "://", size-strlen(str)-1);
	}
	if (proxy->user) {
		strncat(str, proxy->user, size-strlen(str)-1);
		if (proxy->passwd) {
			strncat(str, ":", size-strlen(str)-1);
			strncat(str, proxy->passwd, size-strlen(str)-1);
		}
		strncat(str, "@", size-strlen(str)-1);
	}
	strncat(str, proxy->server, size-strlen(str)-1);
	if (proxy->port) {
		strncat(str, ":", size-strlen(str)-1);
		strncat(str, proxy->port, size-strlen(str)-1);
	}

	rc = string_list_add(envs, str);
	free(str);

	return rc;
}

/* add templates environments to process environments list
  Note: it is needs create env var VZCTL_ENV with full env var name list :
  VZCTL_ENV=:VAR_0:VAR_1:...:VAR_n
*/
int add_tmpl_envs(
	struct tmpl_set *tmpl,
	struct string_list *envs)
{
	char *vzctl_env;
	struct string_list *environments;
	struct string_list_el *p;
	int rc = 0;

	environments = tmplset_get_envs(tmpl);

	if ((rc = fill_vzctl_env_var("VZCTL_ENV=RPM_INSTALL_PREFIX", environments,
		&vzctl_env)))
		return rc;

	string_list_for_each(environments, p)
		string_list_add(envs, p->s);
	string_list_add(envs, vzctl_env);
	free(vzctl_env);

	return rc;
}

#define VIRT_OSRELEASE "/proc/sys/kernel/virt_osrelease"
#define K_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
#define OSRELEASE_SIZE 10
#define STACK_SIZE 2 * 4096

/* Change osrelease by the special way */
static int change_osrelease(char *osrelease, char *out, size_t size)
{
	struct utsname utsbuf;
	int min_a, min_b, min_c;
	int max_a, max_b, max_c;
	int cur_a, cur_b, cur_c;
	char *release_tail;
	int ret;

	if (uname(&utsbuf) != 0) {
		vztt_logger(0, errno, "Unable to get node release, "
			"uname() failed");
		return VZT_INTERNAL;
	}

	if (sscanf(utsbuf.release, "%d.%d.%d", &cur_a, &cur_b,
		&cur_c) != 3) {
		vztt_logger(0, 0, "Unable to parse node release: %s",
			utsbuf.release);
		return VZT_INTERNAL;
	}

	ret = sscanf(osrelease, "%d.%d.%d:%d.%d.%d",
		&min_a, &min_b, &min_c,
		&max_a, &max_b, &max_c);

	if (ret == 3) {
		if (K_VERSION(cur_a, cur_b, cur_c)
			< K_VERSION(min_a, min_b, min_c)) {
			cur_a = min_a; cur_b = min_b; cur_c = min_c;
		} else {
			// Leave the current
			return 0;
		}
	} else if (ret == 6) {
		if (K_VERSION(cur_a, cur_b, cur_c)
			> K_VERSION(max_a, max_b, max_c)) {
			cur_a = max_a; cur_b = max_b; cur_c = max_c;
		} else if (K_VERSION(cur_a, cur_b, cur_c)
			< K_VERSION(min_a, min_b, min_c)) {
			cur_a = min_a; cur_b = min_b; cur_c = min_c;
		} else {
			// Leave the current
			return 0;
		}
	} else {
		vztt_logger(0, 0, "Incorrect osrelease syntax: %s", osrelease);
		return VZT_TMPL_BROKEN;
	}

	snprintf(out, size, "%d.%d.%d", cur_a, cur_b, cur_c);

	if ((release_tail = strchr(utsbuf.release, '-')) != NULL)
		strncat(out, release_tail, size - strlen(osrelease) - 1);

	return 0;
}

/* Clone the process */
static int run_clone(void *data)
{
	struct clone_params *params;
	char osrelease[OSRELEASE_SIZE] = "";
	int dir_fd, fd, osrelease_fd;
	int rc = 0;

	params = data;

	if (params->osrelease)
		if ((rc = change_osrelease(params->osrelease, osrelease, OSRELEASE_SIZE)))
			return rc;

	/* Apply the osrelease hack */
	if (osrelease[0] != '\0')
	{
		if ((osrelease_fd = open(VIRT_OSRELEASE,
			O_RDWR | O_TRUNC)) < 0) {
			vztt_logger(0, errno, "Can't open " VIRT_OSRELEASE);
			return VZT_CANT_OPEN;
		}
		if ((write(osrelease_fd, osrelease,
			strlen(osrelease))) <= 0)
		{
			vztt_logger(0, errno, "Can't write to " VIRT_OSRELEASE);
			close(osrelease_fd);
			return VZT_CANT_WRITE;
		}
		close(osrelease_fd);
	}

	/* open /dev/null in root, because of it is absent in environments */
	fd = open("/dev/null", O_WRONLY);

	/* Cd to / to make availability to unjump from chroot */
	if ((dir_fd=open("/", O_RDONLY)) < 0) {
		vztt_logger(0, errno, "Can not open / directory");
		return VZT_CANT_OPEN;
	}

	/* Next we chroot() to the target directory */
	if (chroot(params->envdir) < 0) {
		vztt_logger(0, errno, "chroot(%s) failed", params->envdir);
		return VZT_CANT_CHROOT;
	}

	/* Go back */
	if (fchdir(dir_fd) < 0) {
		vztt_logger(0, errno, "fchdir(%s) failed", params->envdir);
		return VZT_CANT_CHDIR;
	}

	/* allow C-c for child */
	signal(SIGINT, SIG_DFL);
	/* allow sigquit for child */
	signal(SIGQUIT, SIG_DFL);

	if (params->reader) {
		close(STDOUT_FILENO);
		close(params->fds[0]);
		dup2(params->fds[1], STDOUT_FILENO);
		close(params->fds[1]);
	} else if ((params->debug == 0) && (fd != -1)) {
		close(STDOUT_FILENO);
		dup2(fd, STDOUT_FILENO);
	}

	if (params->ign_cmd_err && (fd != -1)) {
		close(STDERR_FILENO);
		dup2(fd, STDERR_FILENO);
	}

	execve(params->cmd, params->argv, params->envp);
	vztt_logger(0, errno, "execve(%s...) failed", params->cmd);
	rc = VZT_CANT_EXEC;

	return rc;
}

/* run <cmd> from chroot environment <envdir> with arguments <args>
   and environments <envs>,
   redirect <cmd> output to pipe and read by <reader> */
int run_from_chroot2(
		char *cmd,
		char *envdir,
		int debug,
		int ign_cmd_err,
		struct string_list *args,
		struct string_list *envs,
		char *osrelease,
		int reader(FILE *fp, void *data),
		void *data)
{
	pid_t chpid, pid;
	int status;
	int rc = 0;
	struct stat st;
	struct sigaction act_chld, act_quit, act_int;
	char **argv;
	char **envp;
	size_t i, sza, sze;
	struct string_list_el *p;
	int fds[2];
	FILE *fp;
	struct clone_params params;
	int flags = 0;
        void *stack;
	int sa_flags;

	/* environment directory checking */
	if (envdir == NULL) {
		vztt_logger(0, 0, "Environment directory is not defined");
		return VZT_INTERNAL;
	}
	if (stat(envdir, &st)) {
		vztt_logger(0, 0, "stat(%s) error", envdir);
		return VZT_INTERNAL;
	}
	if (!S_ISDIR(st.st_mode)) {
		vztt_logger(0, 0, "%s exist, but is not directory", envdir);
		return VZT_INTERNAL;
	}

	/* Allocate stack */
        stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANON | MAP_GROWSDOWN, 0, 0);
        if (stack == MAP_FAILED) {
		vztt_logger(0, 0, "Can't allocate stack");
		return VZT_INTERNAL;
        }

	/* copy arguments and environments from lists to array */
	sza = string_list_size(args);
	sze = string_list_size(envs);
	argv = (char **)calloc(sza + 2, sizeof(char *));
	envp = (char **)calloc(sze + 1, sizeof(char *));
	if ((argv == NULL) || (envp == NULL)) {
		vztt_logger(0, errno, "Cannot alloc memory");
		rc = VZT_CANT_ALLOC_MEM;
		goto cleanup_0;
	}
	argv[0] = (char *)cmd;
	for (p = args->tqh_first, i = 1; p != NULL && i < sza + 1; \
			p = p->e.tqe_next, i++)
		argv[i] = p->s;
	argv[sza + 1] = NULL;
	for (p = envs->tqh_first, i = 0; p != NULL && i < sze; \
			p = p->e.tqe_next, i++)
		envp[i] = p->s;
	envp[sze] = NULL;

	if (debug >= 4) {
		vztt_logger(2, 0, "Run %s from chroot(%s) with parameters:", \
				cmd, envdir);
		for (i=0; argv[i]; i++)
			vztt_logger(2, 0, "\t%s", argv[i]);
		vztt_logger(2, 0, "And environments:");
		for (i=0; envp[i]; i++)
			vztt_logger(2, 0, "\t%s", envp[i]);
	}

	if (reader) {
		if (pipe(fds) < 0) {
			vztt_logger(0, errno, "pipe() error");
			rc = VZT_CANT_OPEN;
			goto cleanup_1;
		}
	}

	/* Fill the params */
	params.cmd = cmd;
	params.envdir = envdir;
	params.argv = argv;
	params.envp = envp;
	params.osrelease = osrelease;
	params.fds = fds;
	params.reader = reader;
	params.debug = debug;
	params.ign_cmd_err = ign_cmd_err;
	flags = SIGCHLD;

	if (osrelease)
		flags |= CLONE_NEWUTS;

	sigaction(SIGCHLD, NULL, &act_chld);
	sa_flags = act_chld.sa_flags;
	act_chld.sa_flags = SA_NOCLDSTOP;
	sigaction(SIGCHLD, &act_chld, NULL);

	/* ignore sigquit & sigint in parent */
	sigaction(SIGQUIT, NULL, &act_quit);
	signal(SIGQUIT, SIG_IGN);
	sigaction(SIGINT, NULL, &act_int);
	signal(SIGINT, SIG_IGN);

	if ((chpid = clone(run_clone, stack + STACK_SIZE, flags,
		(void *) &params)) < 0) {
		vztt_logger(0, errno, "clone() failed");
		rc = VZT_CANT_FORK;
		goto cleanup_2;
	}

	if (reader) {
		close(fds[1]);
		if ((fp = fdopen(fds[0], "r")) == NULL) {
			vztt_logger(0, errno, "fdopen() error");
			return VZT_CANT_OPEN;
		}
		rc = reader(fp, data);
		fclose(fp);
		close(fds[0]);
	}

	while ((pid = waitpid(chpid, &status, 0)) == -1)
		if (errno != EINTR)
			break;

	if (pid == chpid) {
		if (WIFEXITED(status)) {
			int retcode;
			if ((retcode = WEXITSTATUS(status))) {
				if (ign_cmd_err) {
					rc = 0;
				} else {
					vztt_logger(0, 0,
						"%s failed, exitcode=%d",
						cmd, retcode);
					rc = VZT_PM_FAILED;
				}
			} else
				rc = 0;
		}
		else if (WIFSIGNALED(status)) {
			vztt_logger(0, 0,  "Got signal %d", WTERMSIG(status));
			rc = VZT_PROG_SIGNALED;
		}
	} else if (pid < 0) {
		vztt_logger(0, errno, "Error in waitpid()");
		rc = VZT_INTERNAL;
	}

cleanup_2:
	sigaction(SIGINT, &act_int, NULL);
	sigaction(SIGQUIT, &act_quit, NULL);
	act_chld.sa_flags = sa_flags;
	sigaction(SIGCHLD, &act_chld, NULL);

cleanup_1:
	free( (void *)argv);
	free( (void *)envp);

cleanup_0:
	munmap(stack, STACK_SIZE);

	return rc;
}

/* run <cmd> from chroot environment <envdir> with arguments <args>
   and environments <envs> */
int run_from_chroot(
		char *cmd,
		char *envdir,
		int debug,
		int ign_cmd_err,
		struct string_list *args,
		struct string_list *envs,
		char *osrelease)
{
	return run_from_chroot2(cmd, envdir, debug, ign_cmd_err,
			args, envs, osrelease, NULL, NULL);
}

/* set package manager root dir */
int pm_set_root_dir(struct Transaction *pm, const char *root_dir)
{
	if (root_dir == NULL)
		return 0;

	/* do not check root_dir existence here : as sample, vzrestore does
	   not create CT root, but vzctl will do it on start/mount.
	   https://jira.sw.ru/browse/PSBM-14977 */
	if ((pm->rootdir = strdup(root_dir)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	return 0;
}

/* create temporary root dir for package manager */
int pm_create_tmp_root(struct Transaction *pm)
{
	VZTT_FREE_STR(pm->rootdir);
	pm->rootdir = strdup(pm->tmpdir);
	return pm->pm_create_root(pm->tmpdir);
}

/* remove temporary root dir for package manager */
int pm_remove_tmp_root(struct Transaction *pm)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/var", pm->tmpdir);
	remove_directory(path);
	return 0;
}

/* set VZFS technologies set according veformat */
int pm_set_veformat(struct Transaction *pm, unsigned long veformat)
{
	int rc;

	if ((rc = vefs_check_kern(veformat)))
		return rc;

	pm->vzfs_technologies = 0;

	return 0;
}

/* add exclude list */
int pm_add_exclude(struct Transaction *pm, const char *str)
{
	char *buf, *t, *p;
	char *saveptr;

	if (str == NULL)
		return 0;

	/* alloc temporary buffer */
	if ((buf = strdup(str)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	/* parse exclude list */
	/* Attn: strtok_r will damage exclude */
	for (p = buf; ;p = NULL) {
		if ((t = strtok_r(p, " 	", &saveptr)) == NULL)
			break;
		string_list_add(&pm->exclude, t);
	}

	/* free buffer */
	free((void *)buf);
	return 0;
}

/* create file in tempoarry directory for apt/yum output */
int pm_create_outfile(struct Transaction *pm)
{
	char path[PATH_MAX+1];
	int td;

	/* create outfile */
	snprintf(path, sizeof(path), "%s/outfile.XXXXXX", pm->tmpdir);
	if ((td = mkstemp(path)) == -1) {
		vztt_logger(0, errno, "mkstemp(%s) error", path);
		return VZT_CANT_CREATE;
	}
	close(td);
	if ((pm->outfile = strdup(path)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	vztt_logger(2, 0, "Temporary output file %s was created", pm->outfile);

	return 0;
}

/* remove outfile */
int pm_remove_outfile(struct Transaction *pm)
{
	if (pm->outfile)
		unlink(pm->outfile);
	pm->outfile = NULL;
	return 0;
}

/* get available (in repos) list for installed packages */
int pm_get_available(
		struct Transaction *pm,
		struct package_list *installed,
		struct package_list *available)
{
	int rc;
	char buf[PATH_MAX+1];
	/* CreateTmpRoot will redefine rootdir */
	char *s_rootdir = pm->rootdir;
	ctid_t s_ctid;

	struct package_list notuse;
	struct string_list packages;
	struct package_list_el *p;

	package_list_init(&notuse);
	string_list_init(&packages);

	for (p = installed->tqh_first; p != NULL; p = p->e.tqe_next) {
		/* get internal package name - for package manager */
		pm->pm_get_short_pkgname(p->p, buf, sizeof(buf));
/*		pm->pm_get_int_pkgname(*pkg, buf, sizeof(buf)); */
		if ((rc = string_list_add(&packages, buf)))
			return rc;
	}

	/* set rootdir in tmpdir - it's mandatory
	   package db should be empty for correct apt/yum
	   available packages seeking */
	if ((rc = pm_create_tmp_root(pm)))
		return rc;

	SET_CTID(s_ctid, pm->ctid);
	pm->ctid[0] = '\0';
	/* run transaction */
	rc = pm_modify(pm, VZPKG_AVAIL, &packages, available, &notuse);

	/* restore settings */
	pm->rootdir = s_rootdir;
	SET_CTID(pm->ctid, s_ctid);
	pm_remove_tmp_root(pm);

	if (pm->debug >= 4) {
		vztt_logger(4, 0, "Available packages are:");
		for (p = available->tqh_first; p != NULL; p = p->e.tqe_next)
			vztt_logger(4, 0, "\t%s %s %s", \
				p->p->name, p->p->evr, p->p->arch);
	}
	package_list_clean_all(&notuse);
	string_list_clean(&packages);
	return rc;
}

/* get into VE vz packages list : read vzpackages file */
int pm_get_installed_vzpkg(
		struct Transaction *pm,
		char *ve_private,
		struct package_list *packages)
{
	int rc;
	char path[PATH_MAX+1];

	snprintf(path, sizeof(path), "%s/templates/vzpackages", ve_private);
	if ((rc = read_nevra(path, packages)) != 0)
		return rc;

	if (pm->debug >= 4) {
		struct package_list_el *p;
		vztt_logger(4, 0, "Installed VZ packages are:");
		for (p = packages->tqh_first; p != NULL; p = p->e.tqe_next)
			vztt_logger(4, 0, "\t%s %s %s", \
				p->p->name, p->p->evr, p->p->arch);
	}
	return 0;
}

/* get installed into VE vz packages list :
   read vzpackages and compare with packages database
   This function will mount VE if needs */
int pm_get_installed_vzpkg2(
		struct Transaction *pm,
		const char *ctid,
		char *ve_private,
		struct package_list *installed)
{
	int rc = 0;
	char path[PATH_MAX+1];
	struct package_list vzpackages;
	struct package_list_el *p;

	package_list_init(&vzpackages);

	/* get vzpackages list */
	snprintf(path, sizeof(path), "%s/templates/vzpackages", ve_private);
	if ((rc = read_nevra(path, &vzpackages)) != 0)
		return rc;
	if (pm->debug >= 4) {
		vztt_logger(4, 0, "VZ packages are:");
		for (p = vzpackages.tqh_first; p != NULL; p = p->e.tqe_next)
			vztt_logger(4, 0, "\t%s %s %s", \
				p->p->name, p->p->evr, p->p->arch);
	}

	/* get full installed packages list */
	if (EMPTY_CTID(ctid)) {
		/* use package database on private, do not mount VE if it's stopped */
		rc = pm->pm_get_install_pkg(pm, installed);
	} else {
		rc = pm_get_installed_pkg_from_ve(pm, ctid, installed);
	}
	if (rc)
		goto cleanup;

	/* remove from installed non-vzpackages */
	package_list_for_each(installed, p) {
		/* Attn: p->p->evr should be _real_ package evr */
		if (pm->pm_package_find_nevra(&vzpackages, p->p) == NULL) {
			/* remove package from list and move to next el */
			p = package_list_remove(installed, p);
		}
	}
	if (pm->debug >= 4) {
		vztt_logger(4, 0, "Installed VZ packages are:");
		for (p = installed->tqh_first; p != NULL; p = p->e.tqe_next)
			vztt_logger(4, 0, "\t%s %s %s", \
				p->p->name, p->p->evr, p->p->arch);
	}
cleanup:
	package_list_clean(&vzpackages);
	return rc;
}

/* To get list of installed packages from VE rpm/deb db.
   Do not touch VE private, mount VE if it is stopped */
int pm_get_installed_pkg_from_ve(
		struct Transaction *pm,
		const char *ctid,
		struct package_list *installed)
{
	int rc;
	vzctl_env_status_t ve_status;
	int mounted = 0;

	/* get VE status */
	if (vzctl2_get_env_status(ctid, &ve_status, ENV_STATUS_ALL))
		return vztt_error(VZT_VZCTL_ERROR, 0, "Can't get status of CT %d: %s",
			ctid, vzctl2_get_last_error());

	if (!(ve_status.mask & (ENV_STATUS_RUNNING | ENV_STATUS_MOUNTED))) {
		/* CT is not mounted or running - will mount it temporary */
		if ((rc = do_vzctl("mount", pm->quiet, 0, 0, (char *) ctid, 1, 0)))
			return rc;
		mounted = 1;
	}
	rc = pm->pm_get_install_pkg(pm, installed);
	if (mounted)
		do_vzctl("umount", pm->quiet, 0, 0, (char *) ctid, 1, 0);
	return rc;
}

/* parse next string :
name [epoch:]version-release[ arch] description
and create structure
important: delimiter is _one_ space, string can not start from space
Debian case: sometime dpkg database (/var/lib/dpkg/status) have not
package architecture field at all, sometime for separate packages only

Used by yum & apt classes for rpm & dpkg output parsing
*/
int parse_p(char *str, struct package **pkg)
{
	char *sp = str;
	char *name, *evr, *arch, *descr;

	/* get name */
	name = sp;

	/* get evr */
	if ((sp = strchr(name, ' ')) == NULL) {
		vztt_logger(0, 0, "Bad query output: %s", str);
		return VZT_CANT_PARSE;
	}
	*sp = '\0';
	evr = ++sp;

	/* get arch */
	if ((sp = strchr(evr, ' ')) == NULL) {
		vztt_logger(0, 0, "Bad query output: %s", str);
		return VZT_CANT_PARSE;
	}
	*sp = '\0';
	arch = ++sp;

	/* get description */
	if ((sp = strchr(arch, ' ')) == NULL) {
		vztt_logger(0, 0, "Bad query output: %s", str);
		return VZT_CANT_PARSE;
	}
	*sp = '\0';
	descr = ++sp;
	/* replace all \n to ' ' in description */
	while ((sp = strchr(descr, '\n')))
		*sp = ' ';

	/* create new */
	if ((*pkg = create_structp(name, arch, evr, descr)) == NULL) {
		vztt_logger(0, errno, "Can't alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	return 0;
}

/* run modify transaction for <packages> */
int pm_modify(struct Transaction *pm,
	pm_action_t action,
	struct string_list *packages,
	struct package_list *added,
	struct package_list *removed)
{
	int rc;

	if ((rc = pm_create_outfile(pm)))
		return rc;

	if ((rc = pm->pm_action(pm, action, packages)))
		return rc;

	/* parse outfile */
	if ((rc = read_outfile(pm->outfile, added, removed)))
		return rc;

	pm_remove_outfile(pm);

	return 0;
}

/* fetch package and create directory in template ares */
int pm_prepare_pkg_area(struct Transaction *pm, struct package *pkg)
{
	int rc;
	char buf[PATH_MAX];
	struct string_list packages;

	char *save_rootdir;
	ctid_t save_ctid;
	int save_debug;
	int save_quiet;

	string_list_init(&packages);

	/* dpkg sometimes does not keep architecture in db */
	if (pm->pm_find_pkg_area(pm, pkg))
		return 0;

	/* CreateTmpRoot will redefine rootdir */
	save_rootdir = pm->rootdir;
	SET_CTID(save_ctid, pm->ctid);
	save_debug = pm->debug;
	save_quiet = pm->quiet;

	pm->ctid[0] = '\0';
	pm->debug = 0;
	pm->quiet = 1;

	/* fetch package */
	pm->pm_get_int_pkgname(pkg, buf, sizeof(buf));
	string_list_add(&packages, buf);
	if (save_debug < 4)
		pm->ign_pm_err = 1;
	rc = pm->pm_action(pm, VZPKG_FETCH, &packages);
	pm->ign_pm_err = 0;

	/* restore settings */
	pm->rootdir = save_rootdir;
	SET_CTID(pm->ctid, save_ctid);
	pm->debug = save_debug;
	pm->quiet = save_quiet;

	string_list_clean(&packages);
	if (rc == 0)
		if (!pm->pm_find_pkg_area(pm, pkg))
			return VZT_CANT_FETCH;

	return rc;
}

/* save metadata file for template <name> */
int pm_save_metadata(struct Transaction *pm, const char *name)
{
	int rc;
	struct stat st;
	char path[PATH_MAX];

	/* remove previously created file with the same name */
	snprintf(path, sizeof(path), "%s/%s/%s/list", \
			pm->tmpldir, pm->basesubdir, pm->datadir);
	if (stat(path, &st) == 0) {
		if (S_ISREG(st.st_mode))
			unlink(path);
	}

	/* check directory and create if needs */
	snprintf(path, sizeof(path), "%s/%s/%s/" PM_LIST_SUBDIR, \
			pm->tmpldir, pm->basesubdir, pm->datadir);
	if (stat(path, &st) == 0) {
		if (!S_ISDIR(st.st_mode)) {
			vztt_logger(0, errno, "%s exist, but is not "
				"a directory", path);
			return VZT_CANT_CREATE;
		}
	} else if (mkdir(path, 0755)) {
		vztt_logger(0, errno, "Can not create %s directory", path);
		return VZT_CANT_CREATE;
	}

	snprintf(path, sizeof(path), "%s/%s/%s/" PM_LIST_SUBDIR "%s", \
			pm->tmpldir, pm->basesubdir, pm->datadir, name);
	if ((rc = move_file(path, pm->outfile))) {
		vztt_logger(0, errno, "Can not move %s to %s",
			pm->outfile, path);
		return rc;
	}
	return 0;
}

/* is packages list up2date or not */
int pm_is_up2date(
		struct Transaction *pm,
		struct string_list *ls,
		struct package_list *installed,
		int *flag)
{
	int rc;
	char path[PATH_MAX];
	struct package_list available;
	struct package_list_el *i;
	struct package_list_el *p;
	char *evr;
	int eval;
	struct string_list_el *s;

	package_list_init(&available);
	for (s = ls->tqh_first; s != NULL; s = s->e.tqe_next) {
		snprintf(path, sizeof(path), "%s/%s/%s/" PM_LIST_SUBDIR "%s", \
				pm->tmpldir, pm->basesubdir, pm->datadir, s->s);
		if (access(path, F_OK)) {
			vztt_logger(0, 0, "metadata not found for %s template",
					s->s);
			return VZT_METADATA_NFOUND;
		}

		/* show only VZ packages */
		/* get list of vz packages from file */
		if ((rc = read_nevra_f(path, &available)))
			return rc;
	}

	*flag = 1;
	for (i = installed->tqh_first; i != NULL; i = i->e.tqe_next) {
		/* find installed in vzpackages */
		if ((p = package_list_find(&available, i->p)) == NULL)
			continue;
		/* compare versions */
		/* yum mix epoch 0 and epoch None
		at available epoch 0 == None
		therefore remove epoch 0 from installed */
		if (i->p->evr[0] == '0' && i->p->evr[1] == ':')
			evr = i->p->evr + 2;
		else
			evr = i->p->evr;

		if (strcmp(p->p->evr, evr) == 0) {
			vztt_logger(3, 0, "%s.%s : %s == %s", \
				p->p->name, p->p->arch, p->p->evr, evr);
			continue;
		}
		/* compare versions */
		if ((rc = pm->pm_ver_cmp(pm, p->p->evr, evr, &eval)))
			return rc;

		if (eval == 1) {
			/* available package pkg is newest then installed */
			vztt_logger(3, 0, "%s.%s : %s > %s", \
				p->p->name, p->p->arch, p->p->evr, evr);
			*flag = 0;
			break;
		}
		else
			vztt_logger(3, 0, "%s.%s : %s < %s", \
				p->p->name, p->p->arch, p->p->evr, evr);
	}
	package_list_clean_all(&available);

	return 0;
}

/* find package with name <pname> in list <lst> */
int pm_find_in_list(
		struct Transaction *pm,
		struct package_list *lst,
		const char *pname)
{
	struct package_list_el *p;

	for (p = lst->tqh_first; p != NULL; p = p->e.tqe_next)
		if ((pm->pm_pkg_cmp(pname, p->p)) == 0)
			return 0;

	return 1;
}

/* check undefined '$*' variable in url.
vars - list of internal variables for package manager */
int pm_check_url(char *url, char *vars[], int force)
{
	char *p;
	int i, lfound;

	/* check non-yum $* variable in url */
	for (p = strchr(url, '$'); p; p = strchr(++p, '$')) {
		lfound = 0;
		for (i = 0; vars[i]; i++) {
			if (strncasecmp(p, vars[i], strlen(vars[i])) == 0) {
				lfound = 1;
				break;
			}
		}
		if (!lfound) {
			vztt_logger(0, 0, "URL %s contents undefined variable\n"
				"\tYou can define this variable in "
				VZTT_URL_MAP
				".\n\tSee OpenVZ Templates Management Guide "
				"for more details.", url);
			if (!force)
				return VZT_INVALID_URL;
		}
	}
	return 0;
}

/* Is name official directory in os template directory dir */
int is_official_dir(const char *name)
{
	if (strcmp(name, "config") == 0)
		/* OS template config directory */
		return 1;

	if (strcmp(name, PM_DATA_DIR_NAME) == 0)
		/* it is local cache directory */
		return 1;

	return 0;
}

/* find package in local cache and remove
 nva - directory name in template area */
int pm_rm_pkg_from_cache(struct Transaction *pm, const char *nva)
{
	char path[PATH_MAX];

	if (pm->pm_find_pkg_in_cache(pm, nva, path, sizeof(path)))
		unlink(path);

	return 0;
}

