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
 * Functions set
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <error.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <sys/statfs.h>
#include <dirent.h>
#include <asm/unistd.h>
#include <linux/unistd.h>
#include <sys/vfs.h>
#include <mntent.h>
#include <time.h>

#include <vzctl/libvzctl.h>

#include "vzcommon.h"
#include "config.h"
#include "vztt_error.h"
#include "util.h"
#include "vztt.h"
#include "progress_messages.h"

unsigned long available_technologies[] = {
	VZ_T_I386,
	VZ_T_X86_64,
	VZ_T_IA64,
	VZ_T_NPTL,
	VZ_T_SYSFS,
	VZ_T_SLM,
	VZ_T_VZFS_HASHDIR,
	VZ_T_VZFS_COWDIR,
	VZ_T_VZFS_MFILES,
	VZ_T_VZFS3,
	VZ_T_VZFS4,
	VZ_T_NFS,
	0
};

static char *get_date()
{
	struct tm *p_tm_time;
	time_t ptime;
	char str[100];

	*str = 0;
	ptime = time(NULL);
	p_tm_time = localtime(&ptime);
	strftime(str, sizeof(str), "%Y-%m-%dT%T%z", p_tm_time);

	return strdup(str);
}

static int loglevel = 0;
static char *logfile = NULL;

void init_logger(const char * log_file, int log_level)
{
	if (logfile)
		free(logfile);
	logfile = strdup(log_file);
	loglevel = log_level;
}

int get_loglevel()
{
	return loglevel;
}

static void write_log_rec(int log_level, int err_num, const char *format, va_list ap)
{
	/* Put log message in log file and debug messages with 
	 * level <= DEBUG_LEVEL to stderr 
	 * Log message format: date script : message : 
	 * err_message(based on errno) 
	 * date script : message : err_message(based on errno) 
	 * errno passed in the function as 
	 * err_num parameter
	 */
	FILE *log_file;
	FILE *out;
	char *p;

	if ((log_level == VZTL_ERR) || (log_level == VZTL_EINFO))
		out = stderr;
	else
		out = stdout;

	p = get_date();
	/* Print formatted message */
	if (loglevel >= log_level) {
		va_list ap_save;
		va_copy(ap_save, ap);
		if (log_level == 0)
			fprintf(out, "Error: ");
		vfprintf(out, format, ap_save);
		va_end(ap_save);
		if (err_num)
			fprintf(out, ": %s", strerror(err_num));
		fprintf(out, "\n");
		if (logfile) {
			if ((log_file = fopen(logfile, "a"))) {
				fprintf(log_file, "%s : ", p);
				if (log_level == 0)
					fprintf(log_file, "Error: ");
				vfprintf(log_file, format, ap);
				if (err_num)
					fprintf(log_file, ": %s", strerror(err_num));

				fprintf(log_file, "\n");
				fclose(log_file);
			}
		}
	}
	fflush(out);
	if (p != NULL) free(p);
}

void vztt_logger(int log_level, int err_num, const char * format, ...)
{
	va_list ap;
	va_start(ap, format);
	write_log_rec(log_level, err_num, format, ap);
	va_end(ap);
}

int vztt_error(int err_code, int err_num, const char * format, ...)
{
	va_list ap;
	va_start(ap, format);
	write_log_rec(0, err_num, format, ap);
	va_end(ap);
	return err_code;
}

void progress(char *stage, int percent, int progress_fd)
{
	if (progress_fd == 0 || fcntl(progress_fd, F_GETFL) == -1
		|| !stage || !stage[0])
		return;

	write(progress_fd, PROGRESS_PERCENT_PREFIX,
		strlen(PROGRESS_PERCENT_PREFIX));
	dprintf(progress_fd, "%i", percent);
	write(progress_fd, PROGRESS_DELIMITER, strlen(PROGRESS_DELIMITER));
	write(progress_fd, PROGRESS_STAGE_PREFIX,
		strlen(PROGRESS_STAGE_PREFIX));
	write(progress_fd, stage, strlen(stage));
	write(progress_fd, PROGRESS_END, strlen(PROGRESS_END));
}

/* check VE state */
int check_ve_state(const char *ctid, int status)
{
	vzctl_env_status_t ve_status;

	if (EMPTY_CTID(ctid)) {
		vztt_logger(0, 0, "CT id is 0");
		return VZT_BAD_PARAM;
	}
	/* get VE status */
	if (vzctl2_get_env_status(ctid, &ve_status, ENV_STATUS_ALL))
		vztt_error(VZT_VZCTL_ERROR, 0, "Can't get status of CT %s: %s",
			ctid, vzctl2_get_last_error());

	if (!(ve_status.mask & status)) {
		if (status == ENV_STATUS_RUNNING) {
			vztt_logger(0, 0, "CT %s is not running", ctid);
			return VZT_VE_NOT_RUNNING;
		}
		else if (status == ENV_STATUS_MOUNTED) {
			vztt_logger(0, 0, "CT %s does not mounted", ctid);
			return VZT_VE_NOT_MOUNTED;
		}
		else if (status == ENV_STATUS_EXISTS) {
			vztt_logger(0, 0, "CT %s does not exist", ctid);
			return VZT_VE_NOT_EXIST;
		}
		else if (status == ENV_STATUS_SUSPENDED) {
			vztt_logger(0, 0, "CT %s does not suspended", ctid);
			return VZT_VE_NOT_SUSPENDED;
		}
		else {
			vztt_logger(0, 0, "CT %s has invalid status", ctid);
			return VZT_VE_INVALID_STATUS;
		}
	}
	return 0;
}

/* check VE state and load ve data */
int check_n_load_ve_config(
	const char *ctid,
	int status, 
	struct global_config *gc,
	struct ve_config *vc)
{
	int rc;

	/* check VE state */
	if ((rc = check_ve_state(ctid, status)))
		return rc;

	/* read VE config */
	if ((rc = ve_config_read(ctid, gc, vc, 0)))
		return rc;

	/* read VE layour version from VE private */
	if ((rc = vefs_get_layout(vc->ve_private, &vc->layout)))
		return rc;

	/* read VEFS version from VE private */
	if ((vc->veformat = vzctl2_get_veformat(vc->ve_private)) == -1)
		return vztt_error(VZT_VZCTL_ERROR, 0, "vzctl_get_veformat(%s): %s",
			vc->ve_private, vzctl2_get_last_error());

	/* OS template check */
	if (vc->tmpl_type != VZ_TMPL_EZ) {
		vztt_logger(0, 0, "CT %s OS template (%s) is not a EZ template", \
			ctid, vc->ostemplate);
		return VZT_NOT_EZ_TEMPLATE;
	}

	return 0;
}

/* parse next string :
name arch [epoch:]version-release
and create structure
*/
int parse_nav(char *str, struct package **pkg)
{
	char *sp = str;
	char *name, *arch, *evr;

	/* init */
	*pkg = NULL;

	// skip leading spaces
	while (*sp && isspace(*sp)) sp++;

	// skip empty or comment strings
	if (!*sp || *sp == '#') return 0;

	name = sp;
	while (*sp && !isspace(*sp)) sp++;
	if (!*sp) return 0;
	*sp = '\0'; sp++;

	/* find arch */
	while (*sp && isspace(*sp)) sp++;
	if (!*sp) return 0;
	arch = sp;
	while (*sp && !isspace(*sp)) sp++;
	if (!*sp) return 0;
	*sp = '\0'; sp++;

	/* find evr */
	while (*sp && isspace(*sp)) sp++;
	if (!*sp) return 0;
	evr = sp;
	while (*sp && !isspace(*sp)) sp++;
	*sp = '\0';

	if ((*pkg = create_structp(name, arch, evr, NULL)) == NULL) {
		vztt_logger(0, errno, "Can't alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	return 0;
}

int arch_is_none(const char *arch)
{
	/* arch "none" synonyms (in lower case )*/
	char *arch_none[] = {"none", "(none)", NULL};
	int i;
	for (i = 0; arch_none[i] != NULL; ++i)
	{
		if (strcasecmp(arch, arch_none[i]) == 0)
			return 1;
	}
	return 0;
}

/* Calls VE script */
int call_VE_script(
	const char *ctid,
	const char *script,
	const char *ve_root,
	struct string_list *environment,
	int progress_fd)
{
	char buf[PATH_MAX];
	char tmp_dir[PATH_MAX];
	char tmp_file[PATH_MAX];
	char progress_stage[PATH_MAX];
	char *env = NULL;
	char *vzctl_env = NULL;
	char *script_name;
	int dst;
	int rc = 0;
	int len = 0;
	struct string_list_el *p;

	if (access(script, X_OK)) return 0;

	script_name = strrchr(script, '/');
	script_name++;
	snprintf(progress_stage, sizeof(progress_stage), PROGRESS_RUN_SCRIPT,
		script_name ? script_name : script);
	progress(progress_stage, 0, progress_fd);

	/* Create temp dir */
	snprintf(tmp_dir, sizeof(tmp_dir), "%s/vzttXXXXXX", ve_root);
	if (mkdtemp(tmp_dir) == NULL) {
		vztt_logger(0, errno, "mkdtemp(%s) error", tmp_dir);
		return VZT_CANT_CREATE;
	}

	/* Create temp file */
	snprintf(tmp_file, sizeof(tmp_file), "%s/script", tmp_dir);
	if ((dst = open(tmp_file, O_WRONLY|O_CREAT|O_TRUNC, 0700)) == -1) {
		vztt_logger(0, errno, "open(%s) error", tmp_file);
		return VZT_CANT_CREATE;
	}
	if ((rc = copy_file_fd(dst, tmp_file, script))) {
		close(dst);
		remove_directory(tmp_dir);
		return rc;
	}
	close(dst);

	// Prepare the environment
	if (environment && !string_list_empty(environment)) {
		if ((rc = fill_vzctl_env_var("VZCTL_ENV=",
			environment, &vzctl_env)))
			return rc;
		len += strlen(vzctl_env);
		string_list_for_each(environment, p)
			len += strlen(p->s) + 1;
		if ((env = malloc(len + 1)) == NULL) {
			rc = VZT_CANT_ALLOC_MEM;
			goto cleanup;
		}
		env[0] = '\0';
		string_list_for_each(environment, p) {
			strncat(env, p->s, len);
			strncat(env, " ", len);
		}
		strncat(env, vzctl_env, len);
	}

        // Run script
       	snprintf(buf, sizeof(buf), "%s %s exec2 %s \"bash %s\"", env ? env : "",
       		VZCTL, ctid, tmp_file + strlen(ve_root));
	vztt_logger(3, 0, buf);
	if (system(buf)) {
		vztt_logger(0, 0, "%s error", buf);
		rc = VZT_CANT_EXEC;
		goto cleanup;
	}
	remove_directory(tmp_dir);
cleanup:
	VZTT_FREE_STR(vzctl_env);
	VZTT_FREE_STR(env);
	progress(progress_stage, 100, progress_fd);
        return rc;
}

/* Calls VE script */
int call_VE0_script(
	const char *script,
	const char *ve_root,
	const char *ctid,
	struct string_list *environment,
	int progress_fd)
{
	char buf[PATH_MAX];
	char progress_stage[PATH_MAX];
	char *env = NULL;
	int rc = 0;
	int len = 0;
	struct string_list_el *p;
	char *script_name;

	if (access(script, X_OK)) return 0;

	script_name = strrchr(script, '/');
	script_name++;
	snprintf(progress_stage, sizeof(progress_stage), PROGRESS_RUN_SCRIPT,
		script_name ? script_name : script);
	progress(progress_stage, 0, progress_fd);

	// Prepare the environment
	if (environment && !string_list_empty(environment)) {
		string_list_for_each(environment, p)
			len += strlen(p->s) + 1;
		if ((env = malloc(len + 1)) == NULL) {
			rc = VZT_CANT_ALLOC_MEM;
			goto cleanup;
		}
		env[0] = '\0';
		string_list_for_each(environment, p) {
			strncat(env, p->s, len);
			strncat(env, " ", len);
		}
	}

        // Run script
	snprintf(buf, sizeof(buf), "%s%s %s %s", env ? env : "", script,
		ve_root, ctid);
	vztt_logger(2, 0, "system(\"%s\")", buf);
	if (system(buf)) {
		vztt_logger(0, 0, "%s error", buf);
		rc = VZT_CANT_EXEC;
	}

cleanup:
	VZTT_FREE_STR(env);
	progress(progress_stage, 100, progress_fd);
        return rc;
}

void erase_structp(struct package *pkg)
{
	if (pkg == NULL)
		return;
	if (pkg->name != NULL)
		free((void *)pkg->name);
	if (pkg->arch != NULL)
		free((void *)pkg->arch);
	if (pkg->evr != NULL)
		free((void *)pkg->evr);
	if (pkg->descr != NULL)
		free((void *)pkg->descr);
	free((void *)pkg);
}

struct package *create_structp(
	char const *name, 
	char const *arch, 
	char const *evr, 
	char const *descr)
{
	struct package *pkg;

	if (name == NULL)
		return NULL;

	pkg = (struct package *)malloc(sizeof(struct package));
	if (pkg == NULL)
		return NULL;

	/* init */
	memset((void *)pkg, 0, sizeof(struct package));

	if ((pkg->name = strdup(name)) == NULL)
		return NULL;

	if (arch != NULL) {
		if (arch_is_none(arch))
			pkg->arch = strdup(ARCH_NONE);
		else
			pkg->arch = strdup(arch);
		if (pkg->arch == NULL)
			return NULL;
	}

	if (evr != NULL)
		if ((pkg->evr = strdup(evr)) == NULL)
			return NULL;

	if (descr != NULL)
		if ((pkg->descr = strdup(descr)) == NULL)
			return NULL;

	return pkg;
}

/* compare package <p1> and <p2> with name and arch */
int cmp_pkg(struct package *p1, struct package *p2)
{
        if (strcmp(p1->name, p2->name))
                return 1;
        /* check arch if it defined */
        if ((p1->arch == NULL) || (p2->arch == NULL))
                return 0;
        if ((strlen(p1->arch) == 0) || (strlen(p2->arch) == 0))
                return 0;
        if (strcmp(p1->arch, p2->arch) == 0)
                return 0;

        return 1;
}

/*
 read packages list in form:
name arch [epoch:]version-release
...
 from file 'path'.
 Removing oudated records
*/
int read_nevra(const char *path, struct package_list *packages)
{
	int rc = 0;
	char str[STRSIZ];
	FILE *fp;
	struct package_list_el *i;
	struct package *p;

	if (!(fp = fopen(path, "r"))) {
		error(0, errno, "fopen(\"%s\") error", path);
		return VZT_CANT_OPEN;
	}

	while (fgets(str, sizeof(str), fp)) {
		if ((rc = parse_nav(str, &p)))
			break;

		if (p == NULL) continue;

		/* remove record with the same name & arch */
		for (i = packages->tqh_first; i != NULL; i = i->e.tqe_next) {
			if (cmp_pkg(i->p, p) == 0) {
				package_list_remove(packages, i);
				break;
			}
		}
		/* add new record in tail */
		if ((rc = package_list_insert(packages, p)))
			break;
	}
	fclose(fp);

	return rc;
}

/*
read_nevra fast: do not check records with the same name-arch
*/
int read_nevra_f(const char *path, struct package_list *packages)
{
	int rc = 0;
	char str[STRSIZ];
	FILE *fp;
	struct package *p;

	if (!(fp = fopen(path, "r"))) {
		error(0, errno, "fopen(\"%s\") error", path);
		return VZT_CANT_OPEN;
	}

	while (fgets(str, sizeof(str), fp)) {
		if ((rc = parse_nav(str, &p)))
			break;

		if (p == NULL) continue;
		if ((rc = package_list_insert(packages, p)))
			break;
	}
	fclose(fp);

	return rc;
}

/* save vzpackages file */
int save_vzpackages(const char *ve_private, struct package_list *packages)
{
	char path[PATH_MAX+1];
	FILE *fp;
	struct package_list_el *i;

	snprintf(path, sizeof(path), "%s/templates/", ve_private);
	if (access(path, F_OK)) {
		int rc;
		if ((rc =  create_dir(path)))
			return vztt_error(VZT_CANT_CREATE, rc, 
					"can't create %s", path);
	}

	snprintf(path, sizeof(path), "%s/templates/vzpackages", ve_private);
	if (!(fp = fopen(path, "w")))
		return vztt_error(VZT_CANT_OPEN, errno, "fopen(%s)", path);

	for (i = packages->tqh_first; i != NULL; i = i->e.tqe_next)
		fprintf(fp, " %-22s %-9s %s\n", \
			i->p->name, i->p->arch, i->p->evr);
	fclose(fp);

	return 0;
}

/* read vzpackages file from os template cache tarball */
int read_tarball(
		const char *tarball,
		struct package_list *packages)
{
	char buf[PATH_MAX+1];
	char cmd[PATH_MAX+1];
	FILE *fd;
	struct package *p;
	int rc = 0;

	/* get vzpackages only */
	get_unpack_cmd(cmd, sizeof(cmd), tarball, ".", "-O ./templates/vzpackages");
	vztt_logger(2, 0, "%s", cmd);
	if ((fd = popen(cmd, "r")) == NULL) {
		vztt_logger(0, errno, "popen(%s)", cmd);
		return VZT_CANT_EXEC;
	}

	while(fgets(buf, sizeof(buf), fd)) {
		// skip all records without leading space
		if (!isspace(*buf)) continue;

		if ((rc = parse_nav(buf, &p))) {
			fclose(fd);
			return rc;
		}

		if (p == NULL) continue;

		if ((rc = package_list_insert(packages, p))) {
			fclose(fd);
			return rc;
		}
	}
	rc = pclose(fd);
	if (WEXITSTATUS(rc)) {
		vztt_logger(0, errno, "Unable to execute %s", cmd);
		return VZT_CANT_EXEC;
	}

	return 0;
}

/*
 read packages list in form:
name arch [epoch:]version-release
...
 from file 'path'.
 Removing oudated records
*/
int read_outfile(const char *path, \
		struct package_list *added, \
		struct package_list *removed)
{
	char str[STRSIZ];
	FILE *fp;
	struct package *pkg;
	int rc = 0;
	struct package_list *packages = added;

	if (!(fp = fopen(path, "r"))) {
		error(0, errno, "fopen(\"%s\") error", path);
		return VZT_CANT_OPEN;
	}

	while (fgets(str, sizeof(str), fp)) {
		char *sp = str;

		// skip leading spaces
		while (*sp && isspace(*sp)) sp++;
		// skip empty or comment strings
		if (!*sp || *sp == '#') continue;

		if ((strncmp(sp, "Installed:", strlen("Installed:")) == 0) || \
			(strncmp(sp, "Dependency Installed:", \
						strlen("Dependency Installed:")) == 0) || \
			(strncmp(sp, "Updated:", strlen("Updated:")) == 0) || \
			(strncmp(sp, "Dependency Updated:", \
						strlen("Dependency Updated:")) == 0)) {
			packages = added;
			continue;
		}
		else if ((strncmp(sp, "Removed:", strlen("Removed:")) == 0) || \
			(strncmp(sp, "Dependency Removed:", \
						strlen("Dependency Removed:")) == 0) || \
			(strncmp(sp, "Replaced:", strlen("Replaced:")) == 0)) {
			packages = removed;
			continue;
		}

		if ((rc = parse_nav(sp, &pkg)))
			break;
		if (pkg == NULL) continue;

		if ((rc = package_list_insert(packages, pkg)))
			break;
	}
	fclose(fp);

	return rc;
}

/* merge 3 list: target, added and removed lists into target
   target is not empty: elems from <added> will add or updates,
   elems from <removed> will removed */
int merge_pkg_lists( \
		struct package_list *added,
		struct package_list *removed,
		struct package_list *target)
{
	int rc = 0;
	struct package_list_el *i;
	struct package_list_el *j;
	struct package *pkg;

	/* remove packages from target list */
	for (i = removed->tqh_first; i != NULL; i = i->e.tqe_next) {
		for (j = target->tqh_first; j != NULL; j = j->e.tqe_next) {
			if (cmp_pkg(i->p, j->p) == 0) {
				package_list_remove(target, j);
				break;
			}
		}
	}
	/* update packages if found, and add if not */
	for (i = added->tqh_first; i != NULL; i = i->e.tqe_next) {
		pkg = NULL;
		/* find package from <added> in <target> */
		for (j = target->tqh_first; j != NULL; j = j->e.tqe_next) {
			if (cmp_pkg(i->p, j->p) == 0) {
				pkg = j->p;
				break;
			}
		}
		if (pkg == NULL) {
			/* not found - add new */
			if ((rc = package_list_add(target, i->p)))
				return rc;
		} else {
			/* update existing */
			/* cmp_pkg compare name & arch too, if arch defined
		  	   therefore redefine arch if it is defined on target only */
			if (i->p->arch) {
				if (pkg->arch)
					free((void *)pkg->arch);
				if ((pkg->arch = strdup(i->p->arch)) == NULL) {
					vztt_logger(0, errno, "Can't alloc memory");
					return VZT_CANT_ALLOC_MEM;
				}
			}
			if (i->p->evr) {
				if (pkg->evr)
					free((void *)pkg->evr);
				if ((pkg->evr = strdup(i->p->evr)) == NULL) {
					vztt_logger(0, errno, "Can't alloc memory");
					return VZT_CANT_ALLOC_MEM;
				}
			}
			if (pkg->descr)
				free((void *)pkg->descr);
			if (i->p->descr) {
				if ((pkg->descr = strdup(i->p->descr)) == NULL) {
					vztt_logger(0, errno, "Can't alloc memory");
					return VZT_CANT_ALLOC_MEM;
				}
			}
			else
				pkg->descr = NULL;
		}
	}
	return 0;
}

/* get veid by name or by id */
int get_veid(const char *str, ctid_t ctid)
{
	int rc = 0;
	char ustr[PATH_MAX];

	if (str == NULL)
		return -1;

	/* convert to utf-8 */
	if (vzctl2_convertstr(str, ustr, sizeof(ustr))) {
		vztt_logger(0, 0, "Can't convert CT name to UTF-8: %s", str);
		rc = VZT_BAD_VE_NAME;
	} else if (vzctl2_get_envid_by_name(ustr, ctid)) {
		vztt_logger(0, 0, "Bad CT name: %s", str);
		rc = VZT_BAD_VE_NAME;
	}

	return rc;
}

/* is it ve id/name */
int is_veid(const char *str, ctid_t ctid)
{
	char ustr[PATH_MAX];
	int rc;

	if (str == NULL)
		return 0;

	if (strlen(str) == 0)
		return 0;

	/* convert to utf-8 */
	rc = 0;
	if (vzctl2_convertstr(str, ustr, sizeof(ustr)) == 0) {
		if (vzctl2_is_env_name_valid(ustr)) {
			if (vzctl2_get_envid_by_name(ustr, ctid) == 0)
				rc = 1;
		}
	}

	return rc;
}

/* get VERSION link content for VZFS version */
int vefs_get_link(unsigned veformat, char *link, unsigned size)
{
	switch (veformat) {
		case VZ_T_VZFS0:
			strncpy(link, VZFS0_VERSION_LINK, size);
			break;
		default:
			vztt_logger(0, 0, "Unknown VEFSTYPE %d", veformat);
			return VZT_UNKNOWN_VEFORMAT;
	}
	return 0;
}

/* save VEFS version into VE private area */
int vefs_save_ver(const char *ve_private, unsigned layout, unsigned veformat)
{
	int rc;
	char path[PATH_MAX];
	char version[MAXVERSIONLEN];

	if (layout == VZT_VE_LAYOUT3)
		snprintf(path, sizeof(path), "%s/%s", ve_private, VERSION_LINK);
	else
		snprintf(path, sizeof(path), "%s/fs/%s", ve_private, VERSION_LINK);

	if ((rc = vefs_get_link(veformat, version, sizeof(version))))
		return rc;

	unlink(path);
	if (symlink(version, path)) {
		vztt_logger(0, errno, "symlink(%s,%s) error", version, path);
		return VZT_CANT_CREATE;
	}

	return 0;
}

/* does kernel support this veformat? */
int vefs_check_kern(unsigned veformat)
{
	unsigned unsupported;
	const char *tname;
	int rc;

	if ((unsupported = vzctl2_check_tech(veformat)) == 0)
		return 0;

	if ((tname = vzctl2_tech2name(veformat)) == NULL) {
		vztt_logger(0, 0, "Unknown VEFSTYPE %d", veformat);
		rc = VZT_UNKNOWN_VEFORMAT;
	} else {
		vztt_logger(0, 0, "VEFSTYPE %s does not supported by kernel", tname);
		rc = VZT_UNSUPPORTED_VEFORMAT;
	}
	return rc;
}

/* read VE layout version from VE private */
int vefs_get_layout(const char *ve_private, unsigned *layout)
{
	char buf[PATH_MAX];
	int rc;
	char version[MAXVERSIONLEN];


	snprintf(buf, sizeof(buf), "%s/" LAYOUT_LINK , ve_private);
	if ((rc = readlink(buf, version, MAXVERSIONLEN)) < 0) { 
		if (errno == ENOENT) {
			*layout = VZT_VE_LAYOUT3;
			return 0;
		} else {
			vztt_logger(0, 0, "Can not get layout from %s/" \
				LAYOUT_LINK , ve_private);
			return VZT_CANT_GET_LAYOUT;
		}
	}

	version[rc] = 0;
	if (strcmp(version, VZT_VE_LAYOUT4_LINK) == 0) {
		*layout = VZT_VE_LAYOUT4;
		return 0;
	}

	if (strcmp(version, VZT_VE_LAYOUT5_LINK) == 0) {
		*layout = VZT_VE_LAYOUT5;
		return 0;
	}

	vztt_logger(0, 0, "Unknown layout %s in %s/" LAYOUT_LINK , \
		version, ve_private);
	return VZT_UNKNOWN_LAYOUT;
}

/* get HW node architecture */
void get_hw_arch(char *buf, int bufsize)
{
	struct utsname utsn;

	uname(&utsn);
	/* x86_64 or ia64 */
	if ( (strcmp(utsn.machine, ARCH_X86_64) == 0) || \
		(strcmp(utsn.machine, ARCH_IA64) == 0)) {
		strncpy(buf, utsn.machine, bufsize);
		return;
	}
	/* for all others -  x86 */
	strncpy(buf, ARCH_X86, bufsize);
	return;
}

static char *available_archs[] = {ARCH_X86, ARCH_X86_64, ARCH_IA64, NULL};

/* get available template architectures as char *[] with last NULL */
char **get_available_archs()
{
	return available_archs;
}

/* is <arch> available template architecture */
int isarch(char *arch)
{
	int i;
	for(i=0; available_archs[i]; i++)
		if (strcmp(arch, available_archs[i]) == 0)
			return 1;
	return 0;
}

/* create directory from path */
int create_dir(const char *path)
{
	char buf[PATH_MAX+1];
	char *ptr;

	if (path[0] != '/')
		return EINVAL;
	if (strlen(path)+1 > sizeof(buf))
		return ENAMETOOLONG;
	strcpy(buf, path);
	/* skip leading slashes */
	for (ptr=(char *)buf; *ptr=='/'; ++ptr);
	while(1) {
		if ((ptr = strchr(ptr, '/')))
			*ptr = '\0';
		if (access(buf, F_OK) == 0) {
			struct stat st;
			if ((stat(buf, &st)))
				return -errno;
			if (!S_ISDIR(st.st_mode))
				return ENOTDIR;
		}
		else {
			if ((mkdir(buf, 0755)))
				return errno;
		}
		if (ptr == NULL)
			break;
		*ptr = '/';
		/* skip leading slashes */
		for (ptr=(char *)ptr; *ptr=='/'; ++ptr);
	}
	return 0;
}

/* Clean strings allocated in parse_url */
void clean_url(struct _url *u)
{
	VZTT_FREE_STR(u->proto);
	VZTT_FREE_STR(u->server);
	VZTT_FREE_STR(u->port);
	VZTT_FREE_STR(u->user);
	VZTT_FREE_STR(u->passwd);
	VZTT_FREE_STR(u->path);
}

/* parse url proto://user:password@server:port/path 
  to separate fields */
int parse_url(const char *url, struct _url *u)
{
	char *protos[] = {"https", "http", "ftp", NULL };
	char *proto = NULL;
	char *server = NULL;
	char *user = NULL;
	char *passwd = NULL;
	char *port = NULL;
	char *path = NULL;
	char *buf, *ptr;
	int lfound = 0;
	int i;

	if ((buf = strdup(url)) == NULL)
		goto enomem;

	/* strip protocol */
	if ((ptr = strchr(buf, ':'))) {
		if (*(ptr+1) == '/') {
			/* ok - it's protocol */
			proto = buf;
			*(ptr++) = '\0';
			for(i=0; protos[i] != NULL; ++i) {
				if (strcmp(proto, protos[i])) continue;
				lfound = 1;
				break;
			}
			if (!lfound)
				goto einval;
			while(*ptr && *ptr=='/') ptr++;
		} else {
			ptr = buf;
		}
	} else
		ptr = buf;

	/* check user */
	if ((server = strchr(ptr, '@')) == NULL) {
		server = ptr;
	} else {
		user = ptr;
		*(server++) = '\0';
		/* user:password pair parse */
		if ((passwd = strchr(user, ':')))
			*(passwd++) = '\0';
	}

	/* seek path */
	if ((path = strchr(server, '/')))
		*(path++) = '\0';

	/* server:port */
	if ((port = strchr(server, ':')))
		*(port++) = '\0';

	if (port) {
		for(ptr = port; *ptr; ptr++)
			if (!isdigit(*ptr))
				goto einval;
	}

	if (proto) {
		if ((u->proto = strdup(proto)) == NULL)
			goto enomem;
	} else 
		u->proto = NULL;

	if (server) {
		if ((u->server = strdup(server)) == NULL)
			goto enomem;
	} else 
		u->server = NULL ;

	if (port) {
		if ((u->port = strdup(port)) == NULL)
			goto enomem;
	} else 
		u->port = NULL ;

	if (user) {
		if ((u->user = strdup(user)) == NULL)
			goto enomem;
	} else 
		u->user = NULL ;

	if (passwd) {
		if ((u->passwd = strdup(passwd)) == NULL)
			goto enomem;
	} else 
		u->passwd = NULL ;

	if (path) {
		if ((u->path = strdup(path)) == NULL)
			goto enomem;
	} else 
		u->path = NULL ;
	goto ereturn;
einval:
	free((void *)buf);
	vztt_logger(0, 0, "Invalid URL: %s", url);
	return VZT_INVALID_URL;
enomem:
	free((void *)buf);
	vztt_logger(0, errno, "Cannot alloc memory");
	return VZT_CANT_ALLOC_MEM;
ereturn:
	free((void *)buf);
	return 0;
}

/* remove directory with content */
int remove_directory(const char *dirname)
{
	char path[PATH_MAX+1];
	DIR * dir;
	char dirent_buf[sizeof(struct dirent) + PATH_MAX + 1];
	struct dirent *de = (struct dirent *) dirent_buf;
	struct dirent *result;
	int retval;
	struct stat st;
	int rc = 0;

	if ((dir = opendir(dirname)) == NULL) {
		vztt_logger(0, errno, "opendir(%s) error", dirname);
		return VZT_CANT_OPEN;
	}

	while (1) {
		if ((retval = readdir_r(dir, de, &result)) != 0)
		{
			vztt_logger(0, retval, "readdir_r(\"%s\") error",
					dirname);
			rc = VZT_CANT_READ;
			break;
		}

		if (result == NULL)
			break;

		if(!strcmp(de->d_name,"."))
			continue;

		if(!strcmp(de->d_name,".."))
			continue;

		snprintf(path, sizeof(path), "%s/%s", dirname, de->d_name);

		if (lstat(path, &st)) {
			vztt_logger(0, errno, "stat(%s) error", path);
			rc = VZT_CANT_LSTAT;
			break;
		}

		if (S_ISDIR(st.st_mode)) {
			if ((rc = remove_directory(path)))
				break;
			continue;
		}
		/* remove regfile, symlink, fifo, socket or device */
		if (unlink(path)) {
			vztt_logger(0, errno, "unlink(%s) error", path);
			rc = VZT_CANT_REMOVE;
			break;
		}
	}
	closedir(dir);

	/* and remove directory */
	if (rc)
		return rc;

	if (rmdir(dirname)) {
		vztt_logger(0, errno, "rmdir(%s) error", dirname);
		rc = VZT_CANT_REMOVE;
	}
	return rc;
}

/* copy from file src to descriptor d */
int copy_file_fd(int d, const char *dst, const char *src)
{
	int s;
	char buf[STRSIZ];
	size_t rs = 0, ws = 0;

	if ((s = open(src, O_RDONLY)) == -1) {
		vztt_logger(0, errno, "open(%s) error", src);
		return VZT_CANT_OPEN;
	}
	while ((rs = read(s, (void *)buf, sizeof(buf))) > 0) {
		if ((ws = write(d, (void *)buf, rs)) == -1)
			break;
	}
	close(s);
	if (ws == -1) {
		vztt_logger(0, errno, "write() to %s error", dst);
		return VZT_CANT_WRITE;
	} else if (rs == -1) {
		vztt_logger(0, errno, "read() from %s error", src);
		return VZT_CANT_READ;
	}

	return 0;
}

/* copy from file src to file dst */
int copy_file(const char *dst, const char *src)
{
	int d;
	struct stat st;
	int rc;
	struct utimbuf ut;

	if (access(dst, F_OK) == 0) {
		vztt_logger(0, 0, "File %s already exist", dst);
		return VZT_FILE_EXIST;
	}

	if (stat(src, &st)) {
		vztt_logger(0, errno, "stat(%s) error", src);
		return VZT_CANT_LSTAT;
	}
#ifdef O_LARGEFILE
	if ((d = open(dst, O_WRONLY|O_CREAT|O_TRUNC|O_LARGEFILE, 0600)) == -1) {
#else
	if ((d = open(dst, O_WRONLY|O_CREAT|O_TRUNC, 0600)) == -1) {
#endif
		vztt_logger(0, errno, "open(%s) error", dst);
		return VZT_CANT_OPEN;
	}
	if ((rc = copy_file_fd(d, dst, src))) {
		close(d);
		unlink(dst);
		return rc;
	}
	close(d);
	ut.actime = st.st_atime;
	ut.modtime = st.st_mtime;

	if (lchown(dst, st.st_uid, st.st_gid))
		vztt_logger(0, errno, "Can set owner for %s", dst);
	if (chmod(dst, st.st_mode & 07777))
		vztt_logger(0, errno, "Can set mode for %s", dst);
	if (utime(dst, &ut))
		vztt_logger(0, errno, "Can set utime for %s", dst);

	return 0;
}

/* move from file src to file dst */
int move_file(const char *dst, const char *src)
{
	int rc;

	if (rename(src, dst) == 0)
		return 0;

	if (errno != EXDEV) {
		vztt_logger(0, errno, "rename(%s, %s) error", src, dst);
		return VZT_CANT_RENAME;
	}

	/* src and dst are not on the same filesystem */
	if (access(dst, F_OK) == 0)
		unlink(dst);

	/* try to copy */
	if ((rc = copy_file(dst, src)))
		return rc;

	/* remove source */
	unlink(src);

	return 0;
}

/* cut off leading and tailing blank symbol from string */
char *cut_off_string(char *str)
{
	char *sp, *ep;

	sp = str;
	// skip leading spaces
	while (*sp && (isspace(*sp) || *sp=='\t')) sp++;
	// skip empty or comment strings
	if (!*sp || *sp == '#')
		return NULL;
	// remove tail spaces and 'newline'
	ep = str + strlen(str) - 1;
	while ((isspace(*ep) || *ep == '\n' || *sp=='\t') && ep >= sp)
		*ep-- = '\0';

	return sp;
}

/*
 read first string from file
 leading and tailing spaces omitted
*/
int read_string(char *path, char **str)
{
	int rc = 0;
	char buf[STRSIZ];
	FILE *fp;
	char *sp;

	if (access(path, F_OK))
		return 0;

	if (!(fp = fopen(path, "r"))) {
		vztt_logger(0, errno, "Can not open %s", path);
		return VZT_CANT_OPEN;
	}

	while (fgets(buf, sizeof(buf), fp)) {
		if ((sp = cut_off_string(buf)) == NULL)
			continue;

		if ((*str = strdup(sp)) == NULL) {
			vztt_logger(0, errno, "Cannot alloc memory");
			rc = VZT_CANT_ALLOC_MEM;
		}
		break;
	}
	fclose(fp);

	return rc;
}

/* execute command and check exit code */
int exec_cmd(char *cmd, int quiet)
{
	int rc, fd0 = -1, fd1 = -1;

	if (quiet) {
		/* if quiet - redirect stdout to /dev/null */
		fd0 = open("/dev/null", O_WRONLY);
		fd1 = open("/dev/null", O_WRONLY);
		/* save stdout */
		dup2(STDOUT_FILENO, fd1);
		/* redirect stdout to /dev/null */
		dup2(fd0, STDOUT_FILENO);
	}
	vztt_logger(3, 0, "system(%s)", cmd);
	rc = system(cmd);
	if (quiet) {
		/* restore stdout */
		dup2(fd1, STDOUT_FILENO);
		close(fd0);
		close(fd1);
	}
	if (rc == -1) {
		vztt_logger(0, errno, "system(%s) error", cmd);
		return VZT_CANT_EXEC;
	}
	if (!WIFEXITED(rc)) {
		vztt_logger(0, 0, "\"%s\" failed", cmd);
		return VZT_CMD_FAILED;
	}
	if (WEXITSTATUS(rc)) {
		vztt_logger(0, 0, "\"%s\" return %d", cmd, WEXITSTATUS(rc));
		return VZT_CMD_FAILED;
	}
	return 0;
}

void execv_cmd_logger(int log_level, int err_num, char **argv)
{
	char buf[STRSIZ];
	char *s = buf;
	char *e = buf + sizeof(buf) - 1;
	int i = 0;
	
	for (i= 0; argv[i] != NULL; i++) {
		s += snprintf(s, e - s, "%s ", argv[i]);
		if (s >= e)
			break;
	}

	vztt_logger(log_level, err_num, "execv(\"%s\")", buf);
}

int do_vzctl(char *cmd, char *ctid, int mod, int mask)
{
	char *argv[8];
	int i = 0;

	argv[i++] = VZCTL;
	argv[i++] = "--skiplock";
	argv[i++] = mask & DO_VZCTL_QUIET ? "--quiet" : "--verbose";
	argv[i++] = cmd;
	argv[i++] = ctid;
	if (strcmp("start", cmd) == 0 || strcmp("mount", cmd) == 0)
		argv[i++] = "--skip_ve_setup";
	else if (strcmp("stop", cmd) == 0 && mask & DO_VZCTL_FAST)
		argv[i++] = "--fast";
	if (mask & DO_VZCTL_WAIT)
		argv[i++] = "--wait";
	argv[i++] = NULL;
	if (mask & DO_VZCTL_LOGGER)
		execv_cmd_logger(2, 0, argv);
	return execv_cmd(argv, mask & DO_VZCTL_QUIET, mod);
}

/* execute command and check exit code */
int execv_cmd(char **argv, int quiet, int mod)
{
	int rc = 0, fd0 = -1, fd1 = -1, sa_flags;
	struct sigaction act_chld, act_quit, act_int;
	pid_t child_pid;
	int status = 0;

	sigaction(SIGCHLD, NULL, &act_chld);
	sa_flags = act_chld.sa_flags;
	act_chld.sa_flags = SA_NOCLDSTOP;
	sigaction(SIGCHLD, &act_chld, NULL);

	/* ignore sigquit & sigint in parent */
	sigaction(SIGQUIT, NULL, &act_quit);
	signal(SIGQUIT, SIG_IGN);
	sigaction(SIGINT, NULL, &act_int);
	signal(SIGINT, SIG_IGN);

	child_pid = fork();
	if (0 == child_pid) {
		/* allow C-c for child */
		signal(SIGINT, SIG_DFL);
		/* allow sigquit for child */
		signal(SIGQUIT, SIG_DFL);
		if (quiet) {
			/* if quiet - redirect stdout to /dev/null */
			fd0 = open("/dev/null", O_WRONLY);
			fd1 = open("/dev/null", O_WRONLY);
			/* save stdout */
			dup2(STDOUT_FILENO, fd1);
			/* redirect stdout to /dev/null */
			dup2(fd0, STDOUT_FILENO);
		}
		execv(argv[0], argv);
		vztt_logger(0, errno, "execv(%s...) failed", argv[0]);
		_exit(VZT_CANT_EXEC);
	} else if (child_pid == -1) {
		vztt_logger(0, errno, "fork() failed");
		rc = mod * VZT_CANT_EXEC;
	} else if (child_pid > 0) {
		vztt_logger(3, 0, "execv(%s...)", argv[0]);
		if (wait(&status) == -1) {
			vztt_logger(0, errno, "wait() error");
			rc = mod * VZT_CMD_FAILED;
		} else {
			if (WIFEXITED(status)) {
				rc = WEXITSTATUS(status);
			} else if (WIFSIGNALED(status)) {
				vztt_logger(0, 0,  "Got signal %d", WTERMSIG(status));
				rc = mod * VZT_CMD_FAILED;
			}
		}
	}

	sigaction(SIGINT, &act_int, NULL);
	sigaction(SIGQUIT, &act_quit, NULL);
	act_chld.sa_flags = sa_flags;
	sigaction(SIGCHLD, &act_chld, NULL);

	return rc;
}

int yum_install_execv_cmd(struct string_list *pkgs, int quiet, int mod)
{
	char *argv[EXECV_CMD_MAX_ARGS];
	int cnt = 0, n;
	struct string_list_el *p;

	if (string_list_empty(pkgs))
		return 0;

	argv[cnt++] = YUM;
	argv[cnt++] = "install";
	argv[cnt++] = "-y";

	n = EXECV_CMD_MAX_ARGS - cnt - 1;
	string_list_for_each(pkgs, p) {
		if (cnt > n)
			return vztt_error(VZT_INTERNAL, 0,
				"Too many arguments");
		argv[cnt++] = p->s;
	}

	return execv_cmd(argv, quiet, mod);
}

int yum_install_execv_cmd_op(char *pkg, int quiet, int mod)
{
	struct string_list pkgs;
	int rc;

	string_list_init(&pkgs);
	string_list_add(&pkgs, pkg);
	rc = yum_install_execv_cmd(&pkgs, quiet, mod);
	string_list_clean(&pkgs);

	return rc;
}

int rpm_remove_execv_cmd(char *rpm, struct options_vztt *opts_vztt)
{
	char *argv[6];
	int i = 0;

	argv[i++] = RPMBIN;
	argv[i++] = "-e";
	if (opts_vztt->flags & OPT_VZTT_FORCE)
		argv[i++] = "--nodeps";
	if (opts_vztt->flags & OPT_VZTT_TEST)
		argv[i++] = "--test";
	argv[i++] = rpm;
	argv[i++] = NULL;
	return execv_cmd(argv, (opts_vztt->flags & OPT_VZTT_QUIET), 1);
}

/* get VE private VZFS root directory */
void get_ve_private_root(
		const char *veprivate,
		unsigned layout,
		char *path,
		size_t size)
{
	if (layout == VZT_VE_LAYOUT3)
		snprintf(path, size, "%s/root", veprivate);
	else
		snprintf(path, size, "%s/fs/root", veprivate);
}

/* free string array */
void free_string_array(char ***a)
{
	int i;

	if (*a == NULL)
		return;

	for (i = 0; (*a)[i]; i++)
		free((void *)(*a)[i]);
	free((void *)*a);
	*a = NULL;
}

/* remove files from dir */
int remove_files_from_dir(const char *dir)
{
	/* remove metadata */
	char path[PATH_MAX+1];
	DIR * d;
	char dirent_buf[sizeof(struct dirent) + PATH_MAX + 1];
	struct dirent *de = (struct dirent *) dirent_buf;
	struct dirent *result;
	int retval;
	struct stat st;
	int rc = 0;

	if (access(dir, F_OK)) {
		if (errno == ENOENT)
			return 0;
	}

	if ((d = opendir(dir)) == NULL) {
		vztt_logger(0, errno, "opendir(\"%s\") error", dir);
		return VZT_CANT_OPEN;
	}

	while (1) {
		if ((retval = readdir_r(d, de, &result)) != 0)
		{
			vztt_logger(0, retval, "readdir_r(\"%s\") error",
					dir);
			rc = VZT_CANT_READ;
			break;
		}

		if (result == NULL)
			break;

		if(strcmp(de->d_name, ".") == 0)
			continue;

		if(strcmp(de->d_name, "..") == 0)
			continue;

		snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
		if (lstat(path, &st))
			continue;

		if (!S_ISDIR(st.st_mode))
			unlink(path);
	}
 
	closedir(d);
	return rc;
}

/* is this <path> on shared nfs? */
int is_nfs(const char *path, int *nfs)
{
	int rc;
	long fstype;

	*nfs = 0;
	if ((rc = get_fstype(path, &fstype)))
		return rc;

	if (fstype == NFS_SUPER_MAGIC)
		*nfs = 1;

	return 0;
}

/* is this <path> on FS shared? */
int is_shared_fs(const char *path, int *shared)
{
	int rc;

	if ((rc = is_nfs(path, shared)))
		return rc;

	return 0;
}

/* get FS type */
int get_fstype(const char *path, long *fstype)
{
	struct statfs stfs;

	if (statfs(path, &stfs))
		return vztt_error(VZT_SYSTEM, errno, "statfs(%s) error", path);
	*fstype = stfs.f_type;

	return 0;
}

/*
 Replace substring url_map[i]->src by url_map[i]->dst in src and place to *dst.
 Only one replacement is possible.
*/
int prepare_url(
		struct url_map_list *url_map,
		char *src,
		char *dst,
		size_t size)
{
	struct url_map_rec *u;
	char *p;
	char c;
	int i;
	/* will use buf as source, and will rewrite it after each replacement */
	char buf[2*PATH_MAX];

	strncpy(buf, src, sizeof(buf));
	strncpy(dst, src, size);
	/* scan all url_map table */
	for (u = url_map->tqh_first; u != NULL; u = u->e.tqe_next) {
		/* no more than 10 replacements for one item */
		for (i = 0; i < 10; i++) {
			if ((p = strstr(buf, u->src)) == NULL)
				break;
			c = *p;
			*p = '\0';
			strncpy(dst, buf, size);
			*p = c;
			strncat(dst, u->dst, size-strlen(dst)-1);
			strncat(dst, p + strlen(u->src), size-strlen(dst)-1);
			/* and refresh source buffer now */
			strncpy(buf, dst, sizeof(buf));
		}
	}

	return 0;
}

unsigned long tmpl_get_cache_type(char *path)
{
	unsigned long cache_type = 0;
	char *start;
	if (path == NULL)
		return 0;

	start = strrchr(path, '/');
	if (start == NULL)
		start = path;

	if (strstr(start, PLOOP_V2_SUFFIX "."))
		cache_type |= VZT_CACHE_TYPE_PLOOP_V2;
	else if (strstr(start, PLOOP_SUFFIX "."))
		cache_type |= VZT_CACHE_TYPE_PLOOP;
	else
		cache_type |= VZT_CACHE_TYPE_HOSTFS;

	if (strstr(start, SIMFS_SUFFIX "."))
		cache_type |= VZT_CACHE_TYPE_SIMFS;

	return cache_type;
}

int tmpl_get_clean_os_name(char *path)
{
	char *p;
	if (path == NULL)
		return -1;

	if ((p = strstr(path, TARLZ4_SUFFIX)))
	{
		if (strlen(p) != TARLZ4_SUFFIX_LEN)
			return -2;
		*p = '\0';
	}
	else if ((p = strstr(path, TARLZRW_SUFFIX)))
	{
		if (strlen(p) != TARLZRW_SUFFIX_LEN)
			return -2;
		*p = '\0';
	}
	else if ((p = strstr(path, TARGZ_SUFFIX)))
	{
		if (strlen(p) != TARGZ_SUFFIX_LEN)
			return -2;
		*p = '\0';
	}
	else
		return -2;

	if ((p = strstr(path, SIMFS_SUFFIX)))
		*p = '\0';

	if ((p = strstr(path, PLOOP_SUFFIX)))
		*p = '\0';

	return 0;
}


int tmpl_get_cache_tar_name(char *path, int size,
				unsigned long archive, unsigned long cache_type,
					const char *tmpldir, const char *osname)
{
	char *storage_suffix = "";
	char *fstype_suffix = "";
	char *archive_suffix = "";
	int n;

	if (cache_type & VZT_CACHE_TYPE_PLOOP_V2)
		storage_suffix = PLOOP_V2_SUFFIX;
	else if (cache_type & VZT_CACHE_TYPE_PLOOP)
		storage_suffix = PLOOP_SUFFIX;

	if (cache_type & VZT_CACHE_TYPE_SIMFS)
		fstype_suffix = SIMFS_SUFFIX;

	switch (archive) {
	case VZT_ARCHIVE_LZ4:
	default:
		archive_suffix = TARLZ4_SUFFIX;
		break;
	case VZT_ARCHIVE_GZ:
		archive_suffix = TARGZ_SUFFIX;
		break;
	case VZT_ARCHIVE_LZRW:
		archive_suffix = TARLZRW_SUFFIX;
		break;
	}

	n = snprintf((path), (size), "%s/cache/%s%s%s%s",
		(tmpldir), (osname), fstype_suffix, storage_suffix, archive_suffix);

	return (n > 0 && n < size) ? 0 : -1;
}


int tmpl_get_cache_tar_by_type(char *path, int size, unsigned long cache_type,
						const char *tmpldir, const char *osname)
{
	const int ARCHIVES[] = {VZT_ARCHIVE_LZ4, VZT_ARCHIVE_LZRW, VZT_ARCHIVE_GZ};
	const int ARCHIVES_COUNT = sizeof(ARCHIVES) / sizeof(ARCHIVES[0]);
	int i;
	struct stat st;

	for (i = 0; i < ARCHIVES_COUNT; ++i) {
		if (tmpl_get_cache_tar_name(path, size, ARCHIVES[i], cache_type, tmpldir, osname) == -1)
			return -2;
		if (stat(path, &st) == 0) {
			/* Should check for prlcompress here */
			if (ARCHIVES[i] == VZT_ARCHIVE_LZRW && stat(PRL_COMPRESS_FP, &st) != 0) {
				vztt_logger(1, 0, PRL_COMPRESS " utility is not found, " \
				    "running " YUM " to install it...");
				if (yum_install_execv_cmd_op(PRL_COMPRESS, 1, 1)) {
					vztt_logger(0, 0, "Failed to install the " PRL_COMPRESS);
					return -1;
				}
			}
			return 0;
		}
	}

	return -1;
}

int old_ploop_cache_exists(unsigned long archive, const char *tmpldir,
					const char *osname)
{
	char path[PATH_MAX+1];

	tmpl_get_cache_tar_name(path, sizeof(path), archive,
		VZT_CACHE_TYPE_SIMFS | VZT_CACHE_TYPE_PLOOP,
		tmpldir, osname);

	if (access(path, F_OK) == 0)
		return 1;

	return 0;
}

int tmpl_get_cache_tar(
	struct global_config *gc,
	char *path,
	int size,
	const char *tmpldir,
	const char *osname)
{
	unsigned long cache_types[4];
	int i = 0;

	/* Case for vefstype "all" */
	if (gc == 0 || gc->veformat == 0)
	{
		/* Supported combinations: simfs + vz4, vz4 + vz4, ploop + ext4 */
		cache_types[0] = VZT_CACHE_TYPE_SIMFS | VZT_CACHE_TYPE_PLOOP_V2;
		cache_types[1] = VZT_CACHE_TYPE_SIMFS | VZT_CACHE_TYPE_PLOOP;
		cache_types[2] = VZT_CACHE_TYPE_SIMFS | VZT_CACHE_TYPE_HOSTFS;
		cache_types[3] = 0;
	}
	else
	{
		cache_types[0] = get_cache_type(gc);
		/* Add old-format ploop here */
		if (cache_types[0] & VZT_CACHE_TYPE_PLOOP_V2)
		{
			cache_types[1] = VZT_CACHE_TYPE_SIMFS | VZT_CACHE_TYPE_PLOOP;
			cache_types[2] = 0;
		}
		else
		{
			cache_types[1] = 0;
		}
	}

	while(cache_types[i] != 0)
	{
		if (tmpl_get_cache_tar_by_type(path, size, cache_types[i], tmpldir, osname) == 0)
			return 0;
		i ++;
	}

	return -1;
}


int tmpl_callback_cache_tar(
	struct global_config *gc,
	const char *tmpldir,
	const char *osname,
	int (*call_fn)(const char *path, void *data),
	void *data)
{
	char path[PATH_MAX+1];

	unsigned long cache_types[4];

	/* vefstype "all" case */
	if (gc == 0 || gc->veformat == 0)
	{
		/* Supported combinations: simfs + vz4, vz4 + vz4, ploop + ext4 */
		cache_types[0] = VZT_CACHE_TYPE_SIMFS | VZT_CACHE_TYPE_PLOOP_V2;
		cache_types[1] = VZT_CACHE_TYPE_SIMFS | VZT_CACHE_TYPE_PLOOP;
		cache_types[2] = VZT_CACHE_TYPE_SIMFS | VZT_CACHE_TYPE_HOSTFS;
		cache_types[3] = 0;
	}
	else
	{
		cache_types[0] = get_cache_type(gc);
		/* Add old-format ploop here */
		if (cache_types[0] & VZT_CACHE_TYPE_PLOOP_V2)
		{
			cache_types[1] = VZT_CACHE_TYPE_SIMFS | VZT_CACHE_TYPE_PLOOP;
			cache_types[2] = 0;
		}
		else
		{
			cache_types[1] = 0;
		}
	}

	int i = -1;
	int rc = 0;
	while(cache_types[++i] != 0)
	{
		/*call 2 times for both archiver*/
		if (tmpl_get_cache_tar_by_type(path, sizeof(path), cache_types[i], tmpldir, osname))
			continue;
		rc = call_fn(path, data);
		if (rc)
			break;
		if (tmpl_get_cache_tar_by_type(path, sizeof(path), cache_types[i], tmpldir, osname))
			continue;
		rc = call_fn(path, data);
		if (rc)
			break;
	}

	return rc;
}

static int remove_tar_file(const char *path, void *data)
{
	unlink(path);
	return 0;
}

void tmpl_remove_cache_tar(const char *tmpldir, const char *osname)
{
	tmpl_callback_cache_tar(0, tmpldir, osname, remove_tar_file, NULL);
}

int get_pack_cmd(char *cmd, int size, const char *file, const char *what, const char *opts)
{
	unsigned long archive = 0;
	char *suffix;
	if ((suffix = strstr(file, TARLZ4_SUFFIX)) && (strlen(suffix) == TARLZ4_SUFFIX_LEN))
		archive = VZT_ARCHIVE_LZ4;
	else if ((suffix = strstr(file, TARLZRW_SUFFIX)) && (strlen(suffix) == TARLZRW_SUFFIX_LEN))
		archive = VZT_ARCHIVE_LZRW;
	else if ((suffix = strstr(file, TARGZ_SUFFIX)) && (strlen(suffix) == TARGZ_SUFFIX_LEN))
		archive = VZT_ARCHIVE_GZ;
	else
		return -1;

	return tar_pack(cmd, size, archive, file, what, opts);
}

int get_unpack_cmd(char *cmd, int size, const char *file, const char *where, const char *opts)
{
	unsigned long archive = 0;
	char *suffix;
	if ((suffix = strstr(file, TARLZ4_SUFFIX)) && (strlen(suffix) == TARLZ4_SUFFIX_LEN))
		archive = VZT_ARCHIVE_LZ4;
	else if ((suffix = strstr(file, TARLZRW_SUFFIX)) && (strlen(suffix) == TARLZRW_SUFFIX_LEN))
		archive = VZT_ARCHIVE_LZRW;
	else if ((suffix = strstr(file, TARGZ_SUFFIX)) && (strlen(suffix) == TARGZ_SUFFIX_LEN))
		archive = VZT_ARCHIVE_GZ;
	else
		return -1;

	return tar_unpack(cmd, size, archive, file, where, opts);
}

int tar_pack(char *cmd, int size, unsigned long archive,
			const char *file, const char *what, const char *opts)
{
	int rc;

	switch (archive) {
	case VZT_ARCHIVE_LZ4:
	default:
		rc = snprintf(cmd, size, TAR " -c %s -O %s | " LZ4 " -z > %s",
			opts, what, file);
		break;
	case VZT_ARCHIVE_LZRW:
		rc = snprintf(cmd, size, TAR " -c %s -O %s | " PRL_COMPRESS " -p > %s",
			opts, what, file);
		break;
	case VZT_ARCHIVE_GZ:
		rc = snprintf(cmd, size, TAR " -z -c %s -f %s %s", opts, file, what);
		break;
	}

	return rc;
}

int tar_unpack(char *cmd, int size, unsigned long archive,
			const char *file, const char *where, const char *opts)
{
	int rc;

	switch (archive) {
	case VZT_ARCHIVE_LZ4:
	default:
		rc = snprintf(cmd, size, LZ4 " -d < %s | " TAR " -x -C %s %s",
			file, where, opts);
		break;
	case VZT_ARCHIVE_LZRW:
		rc = snprintf(cmd, size, PRL_COMPRESS " -u <  %s | " TAR " -x -C %s %s ",
			file, where, opts);
		break;
	case VZT_ARCHIVE_GZ:
		rc = snprintf(cmd, size, TAR " -z -x %s -f %s -C %s", opts, file, where);
		break;
	}

	return rc;
}

int is_disabled(char *val)
{
	if (val && (val[0] == 'N' || val[0] == 'n' || val[0] == '0'))
		return 1;
	else
		return 0;
}

/* Fill the VZCTL_ENV= variable */
int fill_vzctl_env_var(
	char *prefix,
	struct string_list *environments,
	char **vzctl_env)
{
	char *ptr;
	struct string_list_el *p;
	size_t sz;

	/* calculate VZCTL_ENV size */
	sz = strlen(prefix) + 1;

	string_list_for_each(environments, p)
		if ((ptr = strchr(p->s, '=')))
			sz += ptr - p->s + 1;

	/* alloc */
	if ((*vzctl_env = (char *)malloc(sz)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	/* fill and add */
	strncpy(*vzctl_env, prefix, sz);
	string_list_for_each(environments, p)
		if ((ptr = strchr(p->s, '='))) {
			*ptr = '\0';
			strncat(*vzctl_env, ":", sz - strlen(*vzctl_env) - 1);
			strncat(*vzctl_env, p->s, sz - strlen(*vzctl_env) - 1);
			*ptr = '=';
		}

	return 0;
}

static unsigned int get_osrelease(char *osrelease)
{
	unsigned int major, middle, minor;

	if (osrelease == 0 || sscanf(osrelease, "%u.%u.%u", &major, &middle,
		&minor) != 3)
		return 0;

	return major*1000*1000 + middle*1000 + minor;
}

// Compare osreleases
int compare_osrelease(char *osrelease1, char *osrelease2)
{
	if (get_osrelease(osrelease1) > get_osrelease(osrelease2))
		return -1;
	if (get_osrelease(osrelease1) < get_osrelease(osrelease2))
		return 1;

	return 0;
}

static int create_ve_layout_link(unsigned long velayout, char *ve_private) {
	char path[PATH_MAX+1];
	int rc;

	snprintf(path, sizeof(path), "%s/.ve.layout", ve_private);
	if (velayout == VZT_VE_LAYOUT5)
		rc = symlink(VZT_VE_LAYOUT5_LINK, path);
	else
		rc = symlink(VZT_VE_LAYOUT4_LINK, path);

	if (rc != 0)
		vztt_logger(0, errno, "Failed to create %s symlink", path);

	return rc;
}

int create_ve_layout(unsigned long velayout, char *ve_private) {
	char path[PATH_MAX+1];

	if (create_ve_layout_link(velayout, ve_private))
		return VZT_CANT_CREATE;

	if (velayout == VZT_VE_LAYOUT5)
		return 0;

	// SIMFS case
	snprintf(path, sizeof(path), "%s/fs", ve_private);
	if (mkdir(path, 0755)) {
		vztt_logger(0, errno, "mkdir(%s) error", path);
		return VZT_CANT_CREATE;
	}

	return 0;
}
