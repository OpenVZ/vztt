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
 * Config file read/write functions
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
#include <dirent.h>
#include <error.h>
#include <limits.h>
#include <vzctl/libvzctl.h>
#include <ploop/libploop.h>

#include "vztt_error.h"
#include "vzcommon.h"
#include "config.h"
#include "util.h"

#define READ_CONFIG_ALL 0
#define READ_CONFIG_SELECTED 1

const char *unneeded_params[] =
{
	"IP_ADDRESS",
	"HOSTNAME",
	"NAMESERVER",
	"SEARCHDOMAIN",
	"VE_ROOT",
	"VE_PRIVATE",
	"DISTRIBUTION",
	"TECHNOLOGIES",
	"NAME",
	"VEFORMAT",
	"VEID",
	"DISABLED",
	"OSTEMPLATE",
	"RATE",
	"RATEBOUND",
	"OFFLINE_MANAGEMENT",
	"OFFLINE_SERVICE",
	"ORIGIN_SAMPLE",
	"CONFIG_CUSTOMIZED",
	"TEMPLATES",
	NULL
};

static char *unescape_str(char *src)
{
	char *p1, *p2;
	int fl;

	if (src == NULL)
		return NULL;
	p2 = src;
	p1 = p2;
	fl = 0;
	while (*p2) {
		if (*p2 == '\\' && !fl) {
			fl = 1;
			p2++;
		} else {
			*p1 = *p2;
			p1++; p2++;
			fl = 0;
		}
	}
	*p1 = 0;

	return src;
}

/*
 parse config file record of next form:
 VARIABLE=value
 or
 VARIABLE="value"
*/
static int parse_config_rec(char *str, char **var, char **val)
{
	char *sp = str;
	char *ep;

	if (str == NULL) return 0;

	unescape_str(str);
	// skip spaces
	while (*sp && isspace(*sp)) sp++;
	// skip empty or comment strings
	if (!*sp || *sp == '#') return 0;

	// remove tail spaces
	ep = str + strlen(str) - 1;
	while (isspace(*ep) && ep >= sp) *ep-- = '\0';
	if (*ep == '"') *ep = 0;

	*var = sp;
	while (*sp && *sp!='=') sp++;
	if (!*sp) return 0;
	*sp = '\0';
	sp++;
	if (*sp == '"') sp++;
	*val = sp;

	return 1;
}

/* 
 read and parse config file
 VARIABLE=value
 or
 VARIABLE="value"
*/
static int read_config(
	char const *path,
	int (*reader)(char *var, char *val, void *data), 
	void *data)
{
	int rc = 0;
	char str[STRSIZ];
	FILE *fp;
	char *var, *val;

	if ((fp = fopen(path, "r")) == NULL) {
		vztt_logger(0, errno, "fopen(\"%s\") error", path);
		return VZT_CANT_OPEN;
	}

	while (1) {
                errno = 0;
		if ((fgets(str, sizeof(str), fp)) == NULL) {
			if (errno) {
				vztt_logger(0, errno, "fgets(\"%s\") error", path);
				rc = VZT_CANT_READ;
			}
			break;
		}

		if (!parse_config_rec(str, &var, &val)) continue;
		if ((rc = (*reader)(var, val, data)))
			break;
	}
	fclose(fp);

	return rc;
}

/* replace $VEID to VEID value */
static char *replace_VEID(const char *ctid, char *src)
{
	char *srcp;
	char str[STRSIZ];
	char *sp, *se;
	int r, len, veidlen;

	if (src == NULL)
		return NULL;
	/* Skip end '/' */
	se = src + strlen(src) - 1;
	while (se != str && *se == '/') {
		*se = 0;
		se--;
	}
	if ((srcp = strstr(src, "$VEID")))
		veidlen = sizeof("$VEID") - 1;
	else if ((srcp = strstr(src, "${VEID}")))
		veidlen = sizeof("${VEID}") - 1;
	else
		return strdup(src);

	sp = str;
	se = str + sizeof(str);
	len = srcp - src; /* Length of src before $VEID */
	if (len > sizeof(str))
		return NULL;
	memcpy(str, src, len);
	sp += len;
	r = snprintf(sp, se - sp, "%s", ctid);
	sp += r;
	if ((r < 0) || (sp >= se))
		return NULL;
	if (*srcp) {
		r = snprintf(sp, se - sp, "%s", srcp + veidlen);
		sp += r;
		if ((r < 0) || (sp >= se))
		return NULL;
	}
	return strdup(str);
}

/* get non-symlink object full name */
static int get_realpath(const char *path, char *buf, int sz)
{
	char content[PATH_MAX+1];
	struct stat st;
	int i, rc;

	strncpy(buf, path, sz);
	for (i = 0; i < 100; i++) {
		if (lstat(buf, &st)) {
			vztt_logger(0, errno, "stat(\"%s\") error", buf);
			return VZT_CANT_LSTAT;
		}
		if (!S_ISLNK(st.st_mode))
			return 0;
		if ((rc = readlink(buf, content, sizeof(content))) == -1) {
			vztt_logger(0, errno, "readlink(%s) error", buf);
			return VZT_CANT_READ;
		}
		content[rc] = '\0';
		strncpy(buf, content, sz);
	}
	vztt_logger(0, errno, "Can not get %s link content", path);
	return VZT_CANT_READ;
}

/* 
 save parameters in config file
*/
int save_config(const char *config,
	int (*writer)(char const *var, FILE *fp, void *data, int *lsaved),
	void *data)
{
	int rc;
	char tmpfile[PATH_MAX+1];
	char path[PATH_MAX+1];
	FILE *sp, *tp;
	int td;
	struct stat st;
	char str[STRSIZ];
	char *buf, *var, *val;
	int lsaved;

	if ((rc = get_realpath(config, path, sizeof(path))))
		return 0;

	if (stat(path, &st)) {
		vztt_logger(0, errno, "stat(\"%s\") error", path);
		return VZT_CANT_LSTAT;
	} 
	/* get tmp name for target file */
	snprintf(tmpfile, sizeof(tmpfile), "%s.XXXXXX", path);
	if ((td = mkstemp(tmpfile)) == -1) {
		vztt_logger(0, errno, "mkstemp(\"%s\") error", tmpfile);
		return VZT_CANT_OPEN;
	}
	if ((tp = fdopen(td, "w")) == NULL) {
		vztt_logger(0, errno, "fdopen(\"%s\",.) error", tmpfile);
		return VZT_CANT_OPEN;
	}

	if ((sp = fopen(path, "r")) == NULL) {
		vztt_logger(0, errno, "fopen(\"%s\",.) error", path);
		return VZT_CANT_OPEN;
	}

	rc = 0;
	while (1) {
                errno = 0;
		if ((fgets(str, sizeof(str), sp)) == NULL) {
			if (errno) {
				vztt_logger(0, errno, "fgets(\"%s\") error", path);
				rc = VZT_CANT_READ;
			}
			break;
		}

		lsaved = 0;
		buf = strdup(str);
		if (parse_config_rec(buf, &var, &val)) {
			if ((rc = (*writer)(var, tp, data, &lsaved)))
				break;
		}
		free(buf);
		if (lsaved)
			continue;

		if (write(td, str, strlen(str)) == -1) {
			vztt_logger(0, errno, "write(\"%s\") error", tmpfile);
			rc = VZT_CANT_WRITE;
			break;
		}
	}
	fclose(sp);
	/* write all unsaved items in end of file */
	if (rc == 0)
		rc = (*writer)("", tp, data, &lsaved);
	fclose(tp);
	close(td);
	if (rc)
		return rc;

	if (chown(tmpfile, st.st_uid, st.st_gid) == -1)
		return vztt_error(VZT_SYSTEM, errno, "chown()");
	if (chmod(tmpfile, st.st_mode & 07777) == -1)
		return vztt_error(VZT_SYSTEM, errno, "chmod()");
	if ((rc = move_file(path, tmpfile))) {
		vztt_logger(0, 0, "Can not move %s to %s", tmpfile, path);
		return rc;
	}

	return 0;
}

static int pfcache_config_reader(char *var, char *val, void *data)
{
	int rc;
	char *str, *token, *saveptr;
	char path[PATH_MAX+1];

	struct global_config *gc = (struct global_config *)data;

	if (strcmp("PFCACHE_INCLUDES", var) != 0)
		return 0;

	string_list_clean(&gc->csum_white_list);

	/* parse string */
	for (str = val; ;str = NULL) {
		if ((token = strtok_r(str, " 	", &saveptr)) == NULL)
			break;
		/* skip leading slashes */
		while (*token == '/')
			token++;
		/* remove tailing slashes exception one */
		while (token[strlen(token)-1] == '/')
			token[strlen(token)-1] = '\0';
		snprintf(path, sizeof(path), "%s/", token);
		if ((rc = string_list_add(&gc->csum_white_list, path)))
			return rc;
	}

	return 0;
}

static int global_config_reader(char *var, char *val, void *data)
{
	struct global_config *gc = (struct global_config *)data;

	/* set default values */
	if ((strcmp("LOCKDIR", var) == 0)) {
		if ((gc->lockdir = strdup(val))== NULL) {
			vztt_logger(0, errno, "Can`t alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	} else if ((strcmp("TEMPLATE", var) == 0)) {
		if ((gc->template_dir = strdup(val))== NULL) {
			vztt_logger(0, errno, "Can`t alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	} else if ((strcmp("VE_ROOT", var) == 0)) {
		if ((gc->ve_root = strdup(val))== NULL) {
			vztt_logger(0, errno, "Can`t alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	} else if ((strcmp("VE_PRIVATE", var) == 0)) {
		if ((gc->ve_private = strdup(val))== NULL) {
			vztt_logger(0, errno, "Can`t alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	} else if ((strcmp("HTTP_PROXY", var) == 0)) {
		if ((gc->http_proxy = strdup(val))== NULL) {
			vztt_logger(0, errno, "Can`t alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	} else if ((strcmp("HTTP_PROXY_USER", var) == 0)) {
		if ((gc->http_proxy_user = strdup(val))== NULL)
		{
			vztt_logger(0, errno, "Can`t alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	} else if ((strcmp("HTTP_PROXY_PASSWORD", var) == 0)) {
		if ((gc->http_proxy_password = strdup(val))== NULL) {
			vztt_logger(0, errno, "Can`t alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	} else if ((strcmp("VEFORMAT", var) == 0) || (strcmp("VEFSTYPE", var) == 0)) {
		if (strcmp("VEFSTYPE", var) == 0)
			gc->used_vefstype = 1; /*mark that VEFSTYPE is used*/

		if ((strcmp("VEFORMAT", var) == 0) && gc->used_vefstype)
			/*VEFSTYPE was readed previously -> ignore VEFORMAT*/
			return 0;

		if ((gc->veformat = vzctl2_name2tech(val)) == 0) {
			vztt_logger(0, 0, "Unknown %s: %s", var, val);
			return VZT_UNKNOWN_VEFORMAT;
		}

		if (gc->veformat != VZ_T_VZFS0 && \
				gc->veformat != VZ_T_EXT4) {
			vztt_logger(0, 0, "Unknown %s: %s", var, val);
			return VZT_UNKNOWN_VEFORMAT;
		}
	} else if ((strcmp("GOLDEN_IMAGE", var) == 0)) {
		if (is_disabled(val))
			gc->golden_image = 0;
	}

	return pfcache_config_reader(var, val, data);
}

unsigned long get_cache_type(struct global_config *gc)
{
	unsigned long type = 0;
	if (gc->veformat == VZ_T_VZFS0)
		type |= VZT_CACHE_TYPE_SIMFS;

	if (gc->velayout == VZT_VE_LAYOUT5)
		if (ploop_is_large_disk_supported())
			type |= VZT_CACHE_TYPE_PLOOP_V2;
		else
			type |= VZT_CACHE_TYPE_PLOOP;
	else
		type |= VZT_CACHE_TYPE_HOSTFS;

	return type;
}


/* set default values */
void global_config_init(struct global_config *gc)
{
	gc->lockdir = NULL;
	gc->template_dir = NULL;
	gc->ve_root = NULL;
	gc->ve_private = NULL;
	gc->http_proxy = NULL;
	gc->http_proxy_user = NULL;
	gc->http_proxy_password = NULL;
	gc->veformat = 0;
	gc->velayout = 0;

	gc->used_vefstype = 0;
	gc->golden_image = 1;

	string_list_init(&gc->csum_white_list);
}

/* Check that layout is valid, fill if undefined */
static int process_fstype_layout(struct global_config *gc, struct options_vztt *opts_vztt)
{
	/* Use the --vefstype parameter if it was given */
	if (opts_vztt && opts_vztt->vefstype)
	{
		/* Process "all" type especially */
		if (strncmp(opts_vztt->vefstype, "all", 3) == 0)
		{
			gc->veformat = 0;
			return 0;
		}
		else if ((gc->veformat = vzctl2_name2tech(opts_vztt->vefstype)) == 0)
		{
			vztt_logger(0, 0, "Unknown VEFSTYPE: %s", opts_vztt->vefstype);
			return VZT_UNKNOWN_VEFORMAT;
		}
	}

	/* Default VEFORMAT/VEFSTYPE undefined case */
	if (gc->veformat == 0)
	{
		if( !(opts_vztt && (opts_vztt->flags & OPT_VZTT_QUIET)) )
			vztt_logger(VZTL_INFO, 0,	\
			"The VEFSTYPE parameter is not set in the vz global " \
			"configuration file; use the default \"ext4\" value.");
		gc->veformat = VZ_T_VZFS0;
		gc->velayout = VZT_VE_LAYOUT5;
		return 0;
	}

	if (gc->veformat == VZ_T_VZFS0)
	{
		/* simfs case */
		gc->velayout = VZT_VE_LAYOUT4;
	}
	else if (gc->veformat == VZ_T_EXT4)
	{
		/* ext4 case */
		gc->veformat = VZ_T_VZFS0;
		gc->velayout = VZT_VE_LAYOUT5;
	}
	else
	{
		vztt_logger(0, 0, "Unknown VEFSTYPE: %s",
			vzctl2_tech2name(gc->veformat));
		return VZT_UNKNOWN_VEFORMAT;
	}

	if (gc->veformat == VZ_T_VZFS0 &&
		(opts_vztt && opts_vztt->flags & OPT_VZTT_FORCE_OPENAT)) {
		vztt_logger(0, 0, "simfs can not be used with openat");
		return VZT_BAD_PARAM;
	}

	return 0;
}

/* load global OpenVZ config parameters */
int global_config_read(struct global_config *gc, struct options_vztt *opts_vztt)
{
	int rc;

	if ((rc = read_config(VZ_CONFIG, global_config_reader, (void *)gc)))
		 return rc;

	/* Try to read pfcache.conf */
	if (access(PFCACHE_CONFIG, F_OK) == 0 &&
		(rc = read_config(PFCACHE_CONFIG, pfcache_config_reader, (void *)gc)))
		 return rc;

	/* Check layout and vefstype */
	if ((rc = process_fstype_layout(gc, opts_vztt)))
		 return rc;

	/* set default values */
	if (gc->lockdir == NULL) {
		vztt_logger(0, 0, "LOCKDIR is not set");
		return VZT_LOCKDIR_NOTSET;
	}
	if (gc->template_dir == NULL) {
		vztt_logger(0, 0, "TEMPLATE is not set");
		return VZT_TEMPLATE_NOTSET;
	}

	return 0;
}

/* free global conf allocated memory */
void global_config_clean(struct global_config *gc)
{
	VZTT_FREE_STR(gc->lockdir);
	VZTT_FREE_STR(gc->template_dir);
	VZTT_FREE_STR(gc->ve_root);
	VZTT_FREE_STR(gc->ve_private);
	VZTT_FREE_STR(gc->http_proxy);
	VZTT_FREE_STR(gc->http_proxy_user);
	VZTT_FREE_STR(gc->http_proxy_password);
	string_list_clean(&gc->csum_white_list);

	/* and set default values */
	global_config_init(gc);
}

/* ve config reader */
static int ve_config_reader(char *var, char *val, void *data)
{
	int rc;
	struct ve_config *vc = (struct ve_config *)data;
	char *str, *token;
	char *saveptr;

	if ((strcmp("VE_ROOT", var) == 0)) {
		if ((vc->ve_root = strdup(val))== NULL) {
			vztt_logger(0, errno, "Can`t alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	} else if ((strcmp("VE_PRIVATE", var) == 0)) {
		if ((vc->ve_private = strdup(val))== NULL) {
			vztt_logger(0, errno, "Can`t alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	} else if ((strcmp("OSTEMPLATE", var) == 0)) {
		if (*val == '.') {
			vc->tmpl_type = VZ_TMPL_EZ;
			val++;
		}
		vc->ostemplate = strdup(val);
		if (vc->ostemplate == NULL) {
			vztt_logger(0, errno, "Can`t alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	} else if ((strcmp("DISTRIBUTION", var) == 0)) {
		vc->distribution = strdup(val);
		if (vc->distribution == NULL) {
			vztt_logger(0, errno, "Can`t alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	} else if ((strcmp("TEMPLATES", var) == 0)) {
		/* parse string */
		for (str = val; ;str = NULL) {
			if ((token = strtok_r(str, " 	", &saveptr)) == NULL)
				break;
			/* skip leading dot in app template names */
			if (*token == '.') token++;
			if ((rc = string_list_add(&vc->templates, token)))
				return rc;
		}
	} else if ((strcmp("TECHNOLOGIES", var) == 0)) {
		unsigned long tech;

		vc->technologies = 0;

		/* set buffer to lower case */
		for (str = val; *str; ++str)
			*str = tolower(*str);

		/* parse VZFS_TECHNOLOGIES string */
		for (str = val; ;str = NULL) {
			if ((token = strtok_r(str, " 	", &saveptr)) == NULL)
				break;
			if ((tech = vzctl2_name2tech(token)) == 0) {
				vztt_logger(0, 0, \
				"Unknown technology in TECHNOLOGIES: %s", \
						token);
				return VZT_UNKNOWN_TECHNOLOGY;
			}
			vc->technologies += tech;
		}
	} else if ((strcmp("EXCLUDE", var) == 0)) {
		if ((vc->exclude = strdup(val)) == NULL) {
			vztt_logger(0, errno, "Can`t alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	} else if ((strcmp("DISKSPACE", var) == 0)) {
		char *end, *start;
		start = strchr(val, ':');
		if (start != NULL)
		{
			start ++;
			vc->diskspace = strtoll(start, &end, 10);
			if (vc->diskspace == 0) {
				vztt_logger(0, errno, "Can`t read disk space");
				return VZT_CANT_ALLOC_MEM;
			}
		}
	}

	return 0;
}

/* load ve config data from file */
int ve_config_file_read(
		const char *ctid,
		char *path,
		struct global_config *gc,
		struct ve_config *vc,
		int ignore)
{
	int rc;
	char *ptr;

	/* set default values */
	vc->config = strdup(path);
	if (vc->config == NULL) {
		vztt_logger(0, errno, "Can`t alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	SET_CTID(vc->ctid, ctid);

	if ((rc = read_config(path, ve_config_reader, (void *)vc)))
		 return rc;

	/* if VE_ROOT is empty - use it from global config */
	if (vc->ve_root)
		ptr = vc->ve_root;
	else
		ptr = gc->ve_root;
	if (ptr == NULL && !ignore) {
		vztt_logger(0, 0, "VE_ROOT is not set for CT %s", ctid);
		return VZT_VE_ROOT_NOTSET;
	}
	/* replace $VEID */
	if ((vc->ve_root = replace_VEID(ctid, ptr)) == NULL) {
		vztt_logger(0, 0, "Can't replace VEID in %s", ptr);
		return VZT_CANT_REPLACE_VEID;
	}

	/* if VE_PRIVATE is empty - use it from global config */
	if (vc->ve_private)
		ptr = vc->ve_private;
	else
		ptr = gc->ve_private;
	/* check */
	if (ptr == NULL && !ignore) {
		vztt_logger(0, 0, "VE_PRIVATE is not set for CT %s", ctid);
		return VZT_VE_PRIVATE_NOTSET;
	}
	/* replace $VEID */
	if ((vc->ve_private = replace_VEID(ctid, ptr)) == NULL) {
		vztt_logger(0, 0, "Can't replace VEID in %s", ptr);
		return VZT_CANT_REPLACE_VEID;
	}

	if (vc->ostemplate == NULL && !ignore) {
		vztt_logger(0, 0, "OSTEMPLATE is not set for CT %s", ctid);
		return VZT_VE_OSTEMPLATE_NOTSET;
	}

	return 0;
}

/* set defaul values */
void ve_config_init(struct ve_config *vc)
{
	vc->config = NULL;
	vc->ctid[0] = '\0';
	vc->ve_root = NULL;
	vc->ve_private = NULL;
	vc->ostemplate = NULL;
	vc->distribution = NULL;
	string_list_init(&vc->templates);
	vc->technologies = 0;
	vc->exclude = NULL;
	vc->veformat = VZ_T_VZFS0;
	vc->layout = VZT_VE_LAYOUT5;
	vc->tmpl_type = VZ_TMPL_EZ;
	vc->diskspace = 1024*1024*10; // 10Gb in 1k blocks

}

/* load ve config data */
int ve_config_read(
		const char *ctid,
		struct global_config *gc,
		struct ve_config *vc,
		int ignore)
{
	char path[PATH_MAX+1];

	snprintf(path, sizeof(path), "%s/%s.conf", ENV_CONF_DIR, ctid);
	return ve_config_file_read(ctid, path, gc, vc, ignore);
}

/* free global conf allocated memory */
void ve_config_clean(struct ve_config *vc)
{
	VZTT_FREE_STR(vc->config);
	VZTT_FREE_STR(vc->ve_root);
	VZTT_FREE_STR(vc->ve_private);
	VZTT_FREE_STR(vc->ostemplate);
	VZTT_FREE_STR(vc->distribution);
	string_list_clean(&vc->templates);
	VZTT_FREE_STR(vc->exclude);

	/* and set default values */
	ve_config_init(vc);
}

struct ve_config_ostemplate
{
	char *ostemplate;
	int tmpl_type;
};

/* ve config reader for OSTEMPLATE variable */
static int ostemplate_ve_config_reader(char *var, char *val, void *data)
{
	struct ve_config_ostemplate *vc = (struct ve_config_ostemplate *)data;

	if ((strcmp("OSTEMPLATE", var) == 0)) {
		if (*val == '.') {
			vc->tmpl_type = VZ_TMPL_EZ;
			val++;
		}
		if ((vc->ostemplate = strdup(val)) == NULL) {
			vztt_logger(0, errno, "Can`t alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	}

	return 0;
}

/* read OSTEMLATE variable from ve config */
int ve_config_ostemplate_read(const char *ctid, char **ostemplate, int *tmpl_type)
{
	int rc;
	char path[PATH_MAX];
	struct ve_config_ostemplate vc;
	vc.ostemplate = NULL;
	vc.tmpl_type = VZ_TMPL_EZ;

	snprintf(path, sizeof(path), "%s/%s.conf", ENV_CONF_DIR, ctid);
	if ((rc = read_config(path, ostemplate_ve_config_reader, (void *)&vc)))
		return rc;
	*ostemplate = vc.ostemplate;
	*tmpl_type = vc.tmpl_type;

	return 0;
}

/* read OSTEMLATE variable from ve sample */
int ve_file_config_ostemplate_read(char *sample, char **ostemplate)
{
	int rc;
	char path[PATH_MAX];
	struct ve_config_ostemplate vc;
	vc.ostemplate = NULL;

	snprintf(path, sizeof(path), "%s/%s", ENV_CONF_DIR, sample);
	if ((rc = read_config(path, ostemplate_ve_config_reader, (void *)&vc)))
		return rc;
	*ostemplate = vc.ostemplate;

	return 0;
}

/* ve config reader for GOLDEN_IMAGE variable */
static int golden_image_ve_config_reader(char *var, char *val, void *data)
{
	struct global_config *gc = (struct global_config *)data;

	if ((strcmp("GOLDEN_IMAGE", var) == 0)) {
		if (is_disabled(val))
			gc->golden_image = 0;
	}

	return 0;
}

/* read GOLDEN_IMAGE variable from ve sample */
int ve_file_config_golden_image_read(char *sample, struct global_config *gc)
{
	int rc;
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", ENV_CONF_DIR, sample);
	if ((rc = read_config(path, golden_image_ve_config_reader, (void *)gc)))
		return rc;

	return 0;
}

/* ve config reader for TEMPLATES variable */
static int templates_ve_config_reader(char *var, char *val, void *data)
{
	int rc;
	struct string_list *templates = (struct string_list *)data;
	char *str, *token;
	char *saveptr;

	if ((strcmp("TEMPLATES", var) == 0)) {
		/* parse string */
		for (str = val; ;str = NULL) {
			if ((token = strtok_r(str, " 	", &saveptr)) == NULL)
				break;
			/* skip leading dot in app template names */
			if (*token == '.') token++;
			if ((rc = string_list_add(templates, token)))
				return rc;
		}
	}

	return 0;
}

/* read TEMPLATES variable from ve config */
int ve_config_templates_read(const char *ctid, struct string_list *templates)
{
	char path[PATH_MAX];

	string_list_init(templates);
	snprintf(path, sizeof(path), "%s/%s.conf", ENV_CONF_DIR, ctid);
	return read_config(path, templates_ve_config_reader, (void *)templates);
}

/* read TEMPLATES variable from ve sample */
int ve_file_config_templates_read(char *sample, struct string_list *templates)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", ENV_CONF_DIR, sample);
	return read_config(path, templates_ve_config_reader, (void *)templates);
}


struct save_ve_config6
{
	char const *ostemplate;
	char const *distribution;
	struct string_list *templates;
	unsigned long technologies;
	unsigned long veformat;
};

static int ve_config_writer6(char const *var, FILE *fp, void *data, int *lsaved)
{
	struct save_ve_config6 *vc = (struct save_ve_config6 *)data;
	static int ostemplate_saved = 0;
	static int templates_saved = 0;
	static int technologies_saved = 0;
	static int distribution_saved = 0;

	/* this function will call some times with strlen(var) > 0, 
	for each found var, and one time with strlen(var) == 0, to write all 
	unsaved yes fields. */
	*lsaved = 0;
	if ((strlen(var) == 0) || (strcmp("OSTEMPLATE", var) == 0)) {
		if (!ostemplate_saved && vc->ostemplate != NULL) {
			fprintf(fp, "OSTEMPLATE=\".%s\"\n", vc->ostemplate);
			ostemplate_saved = 1;
			*lsaved = 1;
		}
	}
	if ((strlen(var) == 0) || (strcmp("DISTRIBUTION", var) == 0)) {
		if (!distribution_saved && vc->distribution != NULL) {
			fprintf(fp, "DISTRIBUTION=\"%s\"\n", vc->distribution);
			distribution_saved = 1;
			*lsaved = 1;
		}
	}
	if ((strlen(var) == 0) || (strcmp("TEMPLATES", var) == 0)) {
		if (!templates_saved) {
			struct string_list_el *l;
			fprintf(fp, "TEMPLATES=\"");
			for (l = vc->templates->tqh_first; l != NULL; \
					l = l->e.tqe_next)
				fprintf(fp, " .%s", l->s);
			fprintf(fp, "\"\n");
			templates_saved = 1;
			*lsaved = 1;
		}
	}
	if ((strlen(var) == 0) || (strcmp("TECHNOLOGIES", var) == 0)) {
		if (!technologies_saved && vc->technologies) {
			int i;

			fprintf(fp, "TECHNOLOGIES=\"");
			for (i = 0; available_technologies[i]; i++)
				if (vc->technologies & \
						available_technologies[i])
					fprintf(fp, " %s", \
						vzctl2_tech2name(
						available_technologies[i]));
			fprintf(fp, "\"\n");
			technologies_saved = 1;
			*lsaved = 1;
		}
	}
	if ((strlen(var) == 0) || (strcmp("VEFORMAT", var) == 0)) {
		if (!distribution_saved && vc->veformat) {
			fprintf(fp, "VEFORMAT=\"%s\"\n", \
				vzctl2_tech2name(vc->veformat));
			*lsaved = 1;
		}
	}

	if (strlen(var) == 0) {
		/* reset static variables */ 
		ostemplate_saved = 0;
		templates_saved = 0;
		technologies_saved = 0;
		distribution_saved = 0;
	}

	return 0;
}

/* save OSTEMPLATES, DISTRIBUTION, TEMPLATES, TECHNOLOGIES and VEFORMAT
   in VE config */
int ve_config6_save(
		char const *path,
		char const *ostemplate,
		char const *distribution,
		struct string_list *apps,
		unsigned long technologies,
		unsigned long veformat)
{
	int rc;
	struct save_ve_config6 vc;

	vc.ostemplate = ostemplate;
	vc.distribution = distribution;
	vc.templates = apps;
	vc.technologies = technologies;
	vc.veformat = veformat;

	if ((rc = save_config(path, ve_config_writer6, (void *)&vc)))
		 return rc;

	return 0;
}

struct save_ve_config1
{
	struct string_list *templates;
};

static int ve_config_writer1(char const *var, FILE *fp, void *data, int *lsaved)
{
	struct save_ve_config1 *vc = (struct save_ve_config1 *)data;
	static int templates_saved = 0;

	/* this function will call some times with strlen(var) > 0, 
	for each found var, and one time with strlen(var) == 0, to write all 
	unsaved yet fields. */
	*lsaved = 0;
	if ((strlen(var) == 0) || (strcmp("TEMPLATES", var) == 0)) {
		if (!templates_saved) {
			struct string_list_el *l;
			fprintf(fp, "TEMPLATES=\"");
			for (l = vc->templates->tqh_first; l != NULL; \
					l = l->e.tqe_next)
				fprintf(fp, " .%s", l->s);
			fprintf(fp, "\"\n");
			templates_saved = 1;
			*lsaved = 1;
		}
	}

	if (strlen(var) == 0) {
		/* reset static variables */ 
		templates_saved = 0;
	}

	return 0;
}

struct save_ve_config2
{
	struct string_list *templates;
};

static int ve_config_writer2(char const *var, FILE *fp, void *data, int *lsaved)
{
	struct save_ve_config2 *vc = (struct save_ve_config2 *)data;
	static int templates_saved = 0;

	/* this function will call some times with strlen(var) > 0, 
	for each found var, and one time with strlen(var) == 0, to write all 
	unsaved yes fields. */
	*lsaved = 0;
	if ((strlen(var) == 0) || (strcmp("TEMPLATES", var) == 0)) {
		if (!templates_saved) {
			struct string_list_el *l;
			fprintf(fp, "TEMPLATES=\"");
			for (l = vc->templates->tqh_first; l != NULL; \
					l = l->e.tqe_next)
				fprintf(fp, " .%s", l->s);
			fprintf(fp, "\"\n");
			templates_saved = 1;
			*lsaved = 1;
		}
	}

	if (strlen(var) == 0) {
		/* reset static variables */ 
		templates_saved = 0;
	}

	return 0;
}

/* save TEMPLATES in VE config */
int ve_config2_save(
		char const *path,
		struct string_list *apps)
{
	int rc;
	struct save_ve_config2 vc;

	vc.templates = apps;

	if ((rc = save_config(path, ve_config_writer2, (void *)&vc)))
		 return rc;

	return 0;
}

/* save TEMPLATES in VE config */
int ve_config1_save(
		char const *path,
		struct string_list *apps)
{
	int rc;
	struct save_ve_config1 vc;

	vc.templates = apps;

	if ((rc = save_config(path, ve_config_writer1, (void *)&vc)))
		 return rc;

	return 0;
}


static int vztt_config_reader(char *var, char *val, void *data)
{
	struct vztt_config *tc = (struct vztt_config *)data;

	if ((strcmp("HTTP_PROXY", var) == 0)) {
		if ((tc->http_proxy = strdup(val))== NULL) {
			vztt_logger(0, errno, "Can`t alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	} else if ((strcmp("HTTP_PROXY_USER", var) == 0)) {
		if ((tc->http_proxy_user = strdup(val))== NULL) {
			vztt_logger(0, errno, "Can`t alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	} else if ((strcmp("HTTP_PROXY_PASSWORD", var) == 0)) {
		if ((tc->http_proxy_password = strdup(val))== NULL) {
			vztt_logger(0, errno, "Can`t alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	} else if ((strcmp("VZTT_PROXY", var) == 0)) {
		if ((tc->vztt_proxy = strdup(val))== NULL) {
			vztt_logger(0, errno, "Can`t alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	} else if ((strcmp("METADATA_EXPIRE", var) == 0)) {
		char *endp;
		int i = strtol(val, &endp, 10);
		if (*endp == '\0')
			tc->metadata_expire = i;
		else
			vztt_logger(0, 0, \
				"Bad METADATA_EXPIRE in vztt config, use default value");
	} else if ((strcmp("EXCLUDE", var) == 0)) {
		if ((tc->exclude = strdup(val)) == NULL) {
			vztt_logger(0, errno, "Can`t alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	} else if ((strcmp("REPAIR_MIRROR", var) == 0)) {
		if ((tc->repair_mirror = strdup(val)) == NULL) {
			vztt_logger(0, errno, "Can`t alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	} else if ((strcmp("APP_TEMPLATE_AUTODETECTION", var) == 0)) {
		tc->apptmpl_autodetect = (strcasecmp(val, "yes") == 0);
	} else if ((strcmp("ARCHIVE", var) == 0)) {
		if (!strcmp(val, "lz4"))
			tc->archive = VZT_ARCHIVE_LZ4;
		else if (!strcmp(val, "lzrw"))
			tc->archive = VZT_ARCHIVE_LZRW;
		else if (!strcmp(val, "gz"))
			tc->archive = VZT_ARCHIVE_GZ;
		else
			vztt_logger(0, 0, \
				"Bad ARCHIVE in vz config, use default value");
	}

	return 0;
}

/* 
 load url.map file
*/
static int load_url_map(char *name, struct url_map_list *url_map)
{
	int rc;
	char str[STRSIZ];
	FILE *fp;
	char *sp, *ep, *first, *second;

	if (access(name, R_OK))
		return 0;
	if (!(fp = fopen(name, "r"))) {
		vztt_logger(0, errno, "fopen(%s) error", name);
		return VZT_CANT_OPEN;
	}

	while (fgets(str, sizeof(str), fp)) {
		// skip spaces
		sp = str;
		while (*sp && isspace(*sp)) sp++;
		// skip empty or comment strings
		if (!*sp || *sp == '#') continue;

		// remove tail spaces
		ep = str + strlen(str) - 1;
		while (isspace(*ep) && ep >= sp) *ep-- = '\0';
		if (*ep == '"') *ep = 0;

		first = sp;
		while (*sp && !isspace(*sp)) sp++;
		if (!*sp) continue;
		*sp = '\0';
		sp++;
		// skip spaces
		while (*sp && isspace(*sp)) sp++;
		if (!*sp) continue;
		second = sp;

		if ((rc = url_map_list_add(url_map, first, second)))
			return rc;
	}
	fclose(fp);
	return 0;
}

/* set default values */
void vztt_config_init(struct vztt_config *tc)
{
	url_map_list_init(&tc->url_map);
	tc->http_proxy = NULL;
	tc->http_proxy_user = NULL;
	tc->http_proxy_password = NULL;
	tc->vztt_proxy = NULL;
	tc->exclude = NULL;
	tc->metadata_expire = METADATA_EXPIRE_DEF;
	tc->repair_mirror = NULL;
	tc->apptmpl_autodetect = 1;
	tc->archive = VZT_ARCHIVE_LZ4;
}

/* read /etc/vztt/vztt.conf & /etc/vztt/url.map */
int vztt_config_read(char *tmpldir, struct vztt_config *tc)
{
	int rc;
	char path[PATH_MAX + 1];

	snprintf(path, sizeof(path), "%s/conf/vztt/url.map", tmpldir);
	/* Read *_SERVER from url.map only.
	   Some EZ OS templates (ubuntu) add *_SERVER variables to vztt.conf
	   from post install script.
	   During update from 3 to 4 old config will save as .rpmsave */
	if ((rc = load_url_map(path, &tc->url_map)))
		 return rc;
	if ((rc = read_config(VZTT_CONFIG, vztt_config_reader, (void *)tc)))
		 return rc;

	return 0;
}

/* free global conf allocated memory */
void vztt_config_clean(struct vztt_config *tc)
{
	url_map_list_clean(&tc->url_map);
	VZTT_FREE_STR(tc->http_proxy);
	VZTT_FREE_STR(tc->http_proxy_user);
	VZTT_FREE_STR(tc->http_proxy_password);
	VZTT_FREE_STR(tc->vztt_proxy);
	VZTT_FREE_STR(tc->exclude);

	/* and set default values */
	vztt_config_init(tc);
}

/* read proxy configuration from:
1 - in global VZ config
2 - in VZTT config
*/
static int get_proxy_from_config(struct global_config *gc,
				struct vztt_config *tc,
				struct _url *http_proxy,
				struct _url *ftp_proxy,
				struct _url *https_proxy)
{
	char *proxy_url = NULL;
	char *proxy_user = NULL;
	char *proxy_password = NULL;
	int rc;
	struct _url *proxy;

	/* proxy settings from local config
	 will overwrite global proxy settings */
	if (tc->http_proxy) {
		/* use proxy settings from VZTT config */
		proxy_url = tc->http_proxy;
		proxy_user = tc->http_proxy_user;
		proxy_password = tc->http_proxy_password;
	} else if (gc->http_proxy) {
		/* use proxy settings from global VZ config */
		proxy_url = gc->http_proxy;
		proxy_user = gc->http_proxy_user;
		proxy_password = gc->http_proxy_password;
	}

	if (proxy_url == NULL)
		return 0;

	if (strlen(proxy_url) == 0)
		return 0;

	if (strncmp(proxy_url, "http:", strlen("http:")) == 0)
		proxy = http_proxy;
	else if (strncmp(proxy_url, "ftp:", strlen("ftp:")) == 0)
		proxy = ftp_proxy;
	else if (strncmp(proxy_url, "https:", strlen("https:")) == 0)
		proxy = https_proxy;
	else
		proxy = http_proxy;

	if ((rc = parse_url(proxy_url, proxy))) {
		vztt_logger(0, 0, "Proxy parsing error");
		return rc;
	}

	if (proxy_user) {
		proxy->user = strdup(proxy_user);
		if (proxy_password)
			proxy->passwd = strdup(proxy_password);
	}

	return 0;
}

/* read proxy configuration from:
1 - in global VZ config
2 - in VZTT config
3 - http_proxy, ftp_proxy and https_proxy environment variables */
int get_proxy(	struct global_config *gc,
		struct vztt_config *tc,
		struct _url *http_proxy,
		struct _url *ftp_proxy,
		struct _url *https_proxy)
{
	char *ptr;
	int rc;

	/* init */
	clean_url(http_proxy);
	clean_url(ftp_proxy);
	clean_url(https_proxy);

	/* load proxy settings from config files */
	if ((rc = get_proxy_from_config(
			gc, tc, http_proxy, ftp_proxy, https_proxy)))
		return rc;

	/* find in environments */
	if (http_proxy->server == NULL) {
		if ((ptr = getenv(HTTP_PROXY))) {
			if (strlen(ptr)) {
				if ((rc = parse_url(ptr, http_proxy)))
					vztt_logger(0, 0, "Proxy parsing error");
			}
		}
	}
	if (ftp_proxy->server == NULL) {
		if ((ptr = getenv(FTP_PROXY))) {
			if (strlen(ptr)) {
				if ((rc = parse_url(ptr, ftp_proxy)))
					vztt_logger(0, 0, "Proxy parsing error");
			}
		}
	}
	if (https_proxy->server == NULL) {
		if ((ptr = getenv(HTTPS_PROXY))) {
			if (strlen(ptr)) {
				if ((rc = parse_url(ptr, https_proxy)))
					vztt_logger(0, 0, "Proxy parsing error");
			}
		}
	}
	/* if protocol does not defined, use default */
	if (http_proxy->server && (http_proxy->proto == NULL))
		http_proxy->proto = strdup("http");
	if (ftp_proxy->server && (ftp_proxy->proto == NULL))
		ftp_proxy->proto = strdup("ftp");
	if (https_proxy->server && (https_proxy->proto == NULL))
		https_proxy->proto = strdup("https");
	return 0;
}

/* mix technologies:
VZ_T_I386, VZ_T_X86_64, VZ_T_IA64, VZ_T_NPTL, VZ_T_SYSFS
- from OS template
VZ_T_SLM
- from VE config
VZ_T_ZDTM
- zero downtime migration - kernel feature, do not save at VE config
*/
unsigned long get_ve_technologies(
	unsigned long tmpl_technologies,
	unsigned long ve_technologies) 
{
	unsigned long technologies = 0;

	if (tmpl_technologies & VZ_T_I386)
		technologies |= VZ_T_I386;

	if (tmpl_technologies & VZ_T_X86_64)
		technologies |= VZ_T_X86_64;

	if (tmpl_technologies & VZ_T_IA64)
		technologies |= VZ_T_IA64;

	if (tmpl_technologies & VZ_T_NPTL)
		technologies |= VZ_T_NPTL;

	if (tmpl_technologies & VZ_T_SYSFS)
		technologies |= VZ_T_SYSFS;

	if (ve_technologies & VZ_T_SLM)
		technologies |= VZ_T_SLM;

	return 0;
}

/* get list of existing ve */
int get_ve_list(
		struct unsigned_list *ls,
		int selector(const char *ctid, void *data),
		void *data)
{
	int cnt, i;
	vzctl_ids_t *ctids;

	if ((ctids = vzctl2_alloc_env_ids()) == NULL) {
		vztt_logger(0, 0, "Can`t alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}

	if ((cnt = vzctl2_get_env_ids_by_state(ctids, ENV_STATUS_EXISTS)) < 0) {
		vztt_logger(0, 0, "Can't get CT list, retcode=%d", cnt);
		return VZT_CANT_GET_VE_LIST;
	}

	for (i = 0; i < cnt; i++) {
/* TODO:
		if (ctids->ids[i] == 0)
			continue;
*/
		if (selector)
			if (selector(ctids->ids[i], data))
				continue;
/* TODO
		if ((rc = unsigned_list_add(ls, ctids->ids[i])))
			break;
*/
	}
	vzctl2_free_env_ids(ctids);
	return 0;
}

/* Strip unneeded strings and return 0 or pointer inside str */
static char *strip_config_rec(char *str)
{
	char *sp = str;
	char *ep;

	if (str == NULL) return 0;

	// skip spaces
	while (*sp && isspace(*sp)) sp++;
	// skip empty or comment strings
	if (!*sp || *sp == '#') return 0;

	// remove tail spaces
	ep = str + strlen(str);
	while (isspace(*ep) && ep >= sp) *ep-- = '\0';

	return sp;
}

/* copy Container config file src to file dst and parse it */
int copy_container_config_file(const char *dst, const char *src)
{
	int rc = 0, i;
	FILE *fp_src, *fp_dst;
	char string_buf[STRSIZ];
	char *stripped;
	struct stat st;
	struct utimbuf ut;

	if (access(dst, F_OK) == 0) {
		vztt_logger(0, 0, "File %s already exist", dst);
		return VZT_FILE_EXIST;
	}

	if (stat(src, &st)) {
		vztt_logger(0, errno, "stat(%s) error", src);
		return VZT_CANT_LSTAT;
	}

	if ((fp_src = fopen(src, "r")) == NULL)
	{
		vztt_logger(0, 0, "fopen(%s) error", src);
		return VZT_CANT_OPEN;
	}

	if ((fp_dst = fopen(dst, "w")) == NULL)
	{
		vztt_logger(0, 0, "fopen(%s) error", dst);
		fclose(fp_src);
		return VZT_CANT_OPEN;
	}

	while (fgets(string_buf, STRSIZ, fp_src))
	{

		if ((stripped = strip_config_rec(string_buf)) == 0)
			continue;

		for(i = 0; unneeded_params[i] != NULL; i++)
		{
			if(!strncmp(unneeded_params[i], stripped,
					strlen(unneeded_params[i])))
			{
				stripped = 0;
				break;
			}
		}

		if (stripped)
		{
			if (fwrite(stripped, 1, strlen(stripped), fp_dst) <
					strlen(stripped))
			{
				vztt_logger(0, 0, "fwrite() to %s error", dst);
				rc = VZT_CANT_WRITE;
				goto cleanup;
			}
		}
	}

	if (ferror(fp_src) || !feof(fp_src))
	{
		vztt_logger(0, errno, "read() from %s error", src);
		rc = VZT_CANT_READ;
		goto cleanup;
	}

	ut.actime = st.st_atime;
	ut.modtime = st.st_mtime;

	if (lchown(dst, st.st_uid, st.st_gid))
		vztt_logger(0, errno, "Can set owner for %s", dst);
	if (chmod(dst, st.st_mode & 07777))
		vztt_logger(0, errno, "Can set mode for %s", dst);
	if (utime(dst, &ut))
		vztt_logger(0, errno, "Can set utime for %s", dst);

cleanup:
	fclose(fp_src);
	fclose(fp_dst);
	return rc;
}
