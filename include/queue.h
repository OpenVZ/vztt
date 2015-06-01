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
 * Double-linked lists functions declarations
 */

#ifndef __QUEUE_H__
#define __QUEUE_H__

#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/queue.h>
#include "vztt_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define list_for_each(ls, el) \
	for (	(el) = ((ls) != NULL) ? (ls)->tqh_first : NULL; \
		(el) != NULL; \
		(el) = (el)->e.tqe_next)

/* char* double-linked list */
/* sample of using:
	struct string_list urls;
	struct string_list_el *p;

	init_string_list(&urls);

	if (read_string_list(path, &urls))
		return 0;

	for (p = urls.tqh_first; p != NULL; p = p->e.tqe_next) {
		printf("%s\n", p->s);
	}
	clean_string_list(&urls);

  List functions alloc and free <char *>
*/
TAILQ_HEAD(string_list, string_list_el);
struct string_list_el {
	char *s;
	TAILQ_ENTRY(string_list_el) e;
};

/* list initialization */
static inline void string_list_init(struct string_list *ls)
{
	TAILQ_INIT(ls);
}

/* remove all elements and its content */
void string_list_clean(struct string_list *ls);

/* add new element in tail */
int string_list_add(struct string_list *ls, char *str);

/* add new element in head */
int string_list_add_head(struct string_list *ls, char *str);

/* find string <str> in list <ls> */
struct string_list_el *string_list_find(struct string_list *ls, char *str);

/* remove element and its content and return pointer to previous elem */
struct string_list_el *string_list_remove(
		struct string_list *ls,
		struct string_list_el *el);

/*
 read strings from file and add into list
 leading and tailing spaces omitted
 lines, commented by '#' ignored
*/
int string_list_read(const char *path, struct string_list *ls);

/* the same as read_string_list and parse line, 
use space and tab as delimiter */
int string_list_read2(const char *path, struct string_list *ls);

/* 1 if list is empty */
static inline int string_list_empty(struct string_list *ls)
{
	return (ls->tqh_first == NULL)?1:0;
}

/* get size of string list <ls> */
size_t string_list_size(struct string_list *ls);

/* copy string list <ls> to string array <*a> */
int string_list_to_array(struct string_list *ls, char ***a);

/* sort all elements in <tosort> */
int string_list_sort(struct string_list *tosort);

/* copy all elements of <src> to <dst> */
int string_list_copy(struct string_list *dst, struct string_list *src);

#define string_list_for_each(ls, el) list_for_each(ls, el)


/*
 url map list
*/
TAILQ_HEAD(url_map_list, url_map_rec);
struct url_map_rec {
	char *src;
	char *dst;
	TAILQ_ENTRY(url_map_rec) e;
};

/* list initialization */
static inline void url_map_list_init(struct url_map_list *ls)
{
	TAILQ_INIT(ls);
}

int url_map_list_add(struct url_map_list *ls, char *src, char *dst);

/* clean url map list */
void url_map_list_clean(struct url_map_list *ls);

#define url_map_list_for_each(ls, el) list_for_each(ls, el)


/*
 repositories/mirrorlists list
*/
TAILQ_HEAD(repo_list, repo_rec);
struct repo_rec {
	char *url;
	char *id;
	int num;
	TAILQ_ENTRY(repo_rec) e;
};

/* list initialization */
static inline void repo_list_init(struct repo_list *ls)
{
	TAILQ_INIT(ls);
}

/* add new element in tail */
int repo_list_add(struct repo_list *ls, char *url, char *id, int num);

/*
 read strings from file and add into repo list
 leading and tailing spaces omitted
 lines, commented by '#' ignored
*/
int repo_list_read(char *path, char *id, int *num, struct repo_list *ls);

/* find record with url == <str> in list <ls> */
struct repo_rec *repo_list_find(struct repo_list *ls, char *url);

/* 1 if list is empty */
static inline int repo_list_empty(struct repo_list *ls)
{
	return (ls->tqh_first == NULL)?1:0;
}

/* get size of repo list <ls> */
size_t repo_list_size(struct repo_list *ls);

/* clean repo list */
void repo_list_clean(struct repo_list *ls);

/* compare two repo lists: 0 - equals, 1 - differs */
int repo_list_cmp(struct repo_list *ls0, struct repo_list *ls1);

#define repo_list_for_each(ls, el) list_for_each(ls, el)


/*
 Package info list
*/
TAILQ_HEAD(pkg_info_list, pkg_info_list_el);
struct pkg_info_list_el {
	struct pkg_info *i;
	TAILQ_ENTRY(pkg_info_list_el) e;
};

/* list initialization */
static inline void pkg_info_list_init(struct pkg_info_list *ls)
{
	TAILQ_INIT(ls);
}

/* remove all elements and its content */
void pkg_info_list_clean(struct pkg_info_list *ls);

/* add new element in tail */
int pkg_info_list_add(struct pkg_info_list *ls, struct pkg_info *i);

/* 1 if list is empty */
static inline int pkg_info_list_empty(struct pkg_info_list *ls)
{
	return (ls->tqh_first == NULL)?1:0;
}

/* get size of pkg_info list <ls> */
size_t pkg_info_list_size(struct pkg_info_list *ls);

/* copy pkg_info list <ls> to pkg_info array <*a>
   Attn: this function does not copy list element content (pkg_info *) */
int pkg_info_list_to_array(struct pkg_info_list *ls, struct pkg_info ***a);

#define pkg_info_list_for_each(ls, el) list_for_each(ls, el)


/*
 struct package * list function set.
 This functions not alloc and not free content: struct package *.
*/
TAILQ_HEAD(package_list, package_list_el);
struct package_list_el {
	struct package *p;
	TAILQ_ENTRY(package_list_el) e;
};

/* list initialization */
static inline void package_list_init(struct package_list *ls)
{
	TAILQ_INIT(ls);
}

/* remove all elements and its content */
void package_list_clean(struct package_list *ls);

/* remove all elements and content */
#define package_list_clean_all(ls) package_list_clean(ls)

/* insert struct package <p> as element in tail of queue */
int package_list_insert(struct package_list *ls, struct package *p);

/* copy struct package <s> content and add it in tail of queue */
int package_list_add(struct package_list *ls, struct package *s);

/* remove element and its content and return pointer to previous elem */
struct package_list_el *package_list_remove(
		struct package_list *ls,
		struct package_list_el *el);

/* get size of package list <ls> */
size_t package_list_size(struct package_list *ls);

/* find (struct package *) in list<struct package *> 
 for name and arch (if arch is defined) */
struct package_list_el *package_list_find(
		struct package_list *packages,
		struct package *pkg);

/* dump packages list */
void package_list_dump(struct package_list *packages);

/* copy package list <ls> with content to package array <*a> */
int package_list_to_array(struct package_list *ls, struct package ***a);

/* copy all elements of <src> to <dst> */
int package_list_copy(struct package_list *dst, struct package_list *src);

/* 1 if list is empty */
static inline int package_list_empty(struct package_list *ls)
{
	return (ls->tqh_first == NULL)?1:0;
}

#define package_list_for_each(ls, el) list_for_each(ls, el)



/* unsigned list */
TAILQ_HEAD(unsigned_list, unsigned_list_el);
struct unsigned_list_el {
	unsigned u;
	TAILQ_ENTRY(unsigned_list_el) e;
};

/* list initialization */
static inline void unsigned_list_init(struct unsigned_list *ls)
{
	TAILQ_INIT(ls);
}

/* remove all elements */
void unsigned_list_clean(struct unsigned_list *ls);

/* add new element in tail */
int unsigned_list_add(struct unsigned_list *ls, unsigned u);

/* get size of list <ls> */
size_t unsigned_list_size(struct unsigned_list *ls);

/* 1 if list is empty */
static inline int unsigned_list_empty(struct unsigned_list *ls)
{
	return (ls->tqh_first == NULL)?1:0;
}

#define unsigned_list_for_each(ls, el) list_for_each(ls, el)


#ifdef __cplusplus
};
#endif

#endif
