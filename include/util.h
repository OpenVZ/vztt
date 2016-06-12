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
 * miscellaneous functions declarations
 */

#include "config.h"
#include "vzcommon.h"
#include "options.h"

#ifndef _VZTT_UTIL_H_
#define _VZTT_UTIL_H_
#ifdef __cplusplus
extern "C" {
#endif
void init_logger(const char * log_file, int log_level);
int get_loglevel();

#ifndef GFS_MAGIC
#define GFS_MAGIC               (0x01161970)
#endif
#ifndef NFS_SUPER_MAGIC
#define NFS_SUPER_MAGIC                     0x6969
#endif
#ifndef GFS_LOCKNAME_LEN 
#define GFS_LOCKNAME_LEN        64
#endif

/* log levels */
#define VZTL_DEB1	1  // to stdout in debug level >= 1
#define VZTL_ERR		0  // to stderr in all modes with prefix "Error:"
#define VZTL_INFO	-1 // to stdout in all modes
#define VZTL_EINFO	-2 // to stderr in all modes
void vztt_logger(int log_level, int err_num, const char * format, ...);
int vztt_error(int err_code, int err_num, const char * format, ...);

extern int parse_nav(char *str, struct package **pkg);
extern int arch_is_none(const char *arch);
extern int call_VE_script(
	const char *ctid,
	const char *script,
	const char *ve_root,
	struct string_list *environment,
	int progress_fd);
extern int call_VE0_script(
	const char *script,
	const char *ve_root,
	const char *ctid,
	struct string_list *environment,
	int progress_fd);
extern void erase_structp(struct package *pkg);
extern struct package *create_structp(
	char const *name, 
	char const *arch, 
	char const *evr, 
	char const *descr);
/* compare package <p1> and <p2> with name and arch */
extern int cmp_pkg(struct package *p1, struct package *p2);
/* check VE state */
int check_ve_state(const char *ctid, int status);
/* check VE state and load ve data */
int check_n_load_ve_config(
	const char *ctid,
	int status, 
	struct global_config *gconfig,
	struct ve_config *vconfig);
/*
 read packages list in form:
name arch [epoch:]version-release
...
 from file 'path'.
 Removing oudated records
*/
int read_nevra(const char *path, struct package_list *packages);
/*
read_nevra fast: do not check records with the same name-arch
*/
int read_nevra_f(const char *path, struct package_list *packages);
/* save vzpackages file */
int save_vzpackages(const char *ve_private, struct package_list *packages);
/* read vzpackages file from os template cache tarball */
int read_tarball(
		const char *tarball,
		struct package_list *packages);
/*
 read packages list in form:
name arch [epoch:]version-release
...
 from file 'path'.
 Removing oudated records
*/
int read_outfile(const char *path, \
		struct package_list *added, \
		struct package_list *removed);
/* merge 3 list: target, added and removed lists into target
   target is not empty: elems from <added> will add or updates,
   elems from <removed> will removed */
int merge_pkg_lists( \
		struct package_list *added,
		struct package_list *removed,
		struct package_list *target);

/* update OS and apps templates metadata */
int update_metadata(
	char *ostemplate,
	struct global_config *gc,
	struct vztt_config *tc,
	struct options_vztt *opts_vztt);

/* get veid by name or by id */
int get_veid(const char *str, ctid_t ctid);
/* is it veid or ve name */
int is_veid(const char *str, ctid_t ctid);

/* get VERSION link content for VZFS version */
int vefs_get_link(unsigned veformat, char *link, unsigned size);
/* save VEFS version into VE private area */
int vefs_save_ver(const char *ve_private, unsigned layout, unsigned veformat);
/* does kernel support this veformat? */
int vefs_check_kern(unsigned veformat);
/* read VE layout version from VE private */
int vefs_get_layout(const char *ve_private, unsigned *layout);

/* get HW node architecture */
void get_hw_arch(char *buf, int bufsize);
/* get available template architectures as char *[] with last NULL */
char **get_available_archs();
/* is <arch> available template architecture */
int isarch(char *arch);
/* create directory from path, exclude last name
to create with full path, add tail slash */
int create_dir(const char *path);
/* parse url */
int parse_url(const char *url, struct _url *u);
/* clean_url */
void clean_url(struct _url *u);
/*calculation new url with vztt_proxy*/
char * get_url_vztt_proxy(const char *vztt_proxy, const char *url) ;
/* remove directory with content */
int remove_directory(const char *dirname);
/* copy from file src to descriptor d */
int copy_file_fd(int d, const char *dst, const char *src);
/* copy from file src to file dst */
int copy_file(const char *dst, const char *src);
/* move from file src to file dst */
int move_file(const char *dst, const char *src);
/*  execute command and check exit code */
int exec_cmd(char *cmd, int quiet);

/* cut off leading and tailing blank symbol from string */
char *cut_off_string(char *str);

/* get VE private VZFS root directory */
void get_ve_private_root(
		const char *veprivate,
		unsigned layout,
		char *path,
		size_t size);

/* read string from file <path> */
int read_string(char *path, char **str);

/* remove files from dir */
int remove_files_from_dir(const char *dir);

/* free string array */
void free_string_array(char ***a);

#define VZTT_FREE_STR(str) \
		if ((str)) { \
			free((void *)(str)); \
			(str) = NULL; \
		}

/* is this <path> on shared nfs? */
int is_nfs(const char *path, int *nfs);

/* is this <path> on FS shared? */
int is_shared_fs(const char *path, int *shared);

/* get FS type */
int get_fstype(const char *path, long *fstype);

/*
 Replace substring url_map[i]->src by url_map[i]->dst in src and place to *dst.
 Only one replacement is possible.
*/
int prepare_url(
		struct url_map_list *url_map,
		char *src,
		char *dst,
		size_t size);


/*
return cache type of file. detect by name of file
*/
unsigned long tmpl_get_cache_type(char *path);

/*
Strip postfixed like 'ploop', 'simfs', 'tar.gz', 'tar.lzrw'
and leave only osname.
Function modify path variable.
*/
int tmpl_get_clean_os_name(char *path);

/*
generate path to os template cache file.
*/
int tmpl_get_cache_tar_name(char *path, int size,
				unsigned long archive, unsigned long cache_type,
					const char *tmpldir, const char *osname);



int tmpl_get_cache_tar_by_type(char *path, int size, unsigned long cache_type,
				const char *tmpldir, const char *osname);

/*
get path to the os template cache file.
*/
int tmpl_get_cache_tar(
	struct global_config *gc,
	char *path,
	int size,
	const char *tmpldir,
	const char *osname);


void tmpl_remove_cache_tar(const char *tmpldir, const char *osname);

int tmpl_callback_cache_tar(
	struct global_config *gc,
	const char *tmpldir,
	const char *osname,
	int (*call_fn)(const char *path, void *data),
	void *data);


/*
generate cmd command for pack 'what' to 'file'. archiver is detected
automatically from 'file' extension
*/
int get_pack_cmd(char *cmd, int size,
			const char *file, const char *what, const char *opts);

/*
generate cmd command for unpack 'file' to 'where'. archiver is detected
automatically from 'file' extension
*/
int get_unpack_cmd(char *cmd, int size,
			const char *file, const char *where, const char *opts);

/*
generate cmd command for pack 'what' to 'file' with 'archive' type (gz, lzrw
or lz4)
*/
int tar_pack(char *cmd, int size, unsigned long archive,
			const char *file, const char *what, const char *opts);

/*
generate cmd command for unpack 'file' to 'where' with 'archive' type (gz,
lzrw or lz4)
*/
int tar_unpack(char *cmd, int size, unsigned long archive,
			const char *file, const char *where, const char *opts);


/* Check the string for predefined disable symbols */
int is_disabled(char *val);

/* Fill the VZCTL_ENV= variable */
int fill_vzctl_env_var(
	char *prefix,
	struct string_list *environments,
	char **vzctl_env);

void progress(char *stage, int percent, int progress_fd);

int old_ploop_cache_exists(unsigned long archive, const char *tmpldir,
					const char *osname);

int compare_osrelease(char *osrelease1, char *osrelease2);

int create_ve_layout(unsigned long velayout, char *ve_private);

#ifdef __cplusplus
}
#endif

#endif
