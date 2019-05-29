/*
 * Copyright (c) 2015-2017, Parallels International GmbH
 * Copyright (c) 2017-2019 Virtuozzo International GmbH. All rights reserved.
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
 * Our contact details: Virtuozzo International GmbH, Vordergasse 59, 8200
 * Schaffhausen, Switzerland.
 *
 * queues
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <dirent.h>
#include <linux/unistd.h>

#include "vztt_error.h"
#include "vzcommon.h"
#include "util.h"


/* 
 char* double-linked list 
*/
static struct string_list_el *string_list_create_el(char *str)
{
	struct string_list_el *p;

	p = (struct string_list_el *)malloc(sizeof(struct string_list_el));
	if (p == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return NULL;
	}
	if ((p->s = strdup(str)) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		free((void *)p);
		return NULL;
	}
	return p;
}

/* add new element in tail */
int string_list_add(struct string_list *ls, char *str)
{
	struct string_list_el *p;

	p = string_list_create_el(str);
	if (p == NULL)
		return VZT_CANT_ALLOC_MEM;
	TAILQ_INSERT_TAIL(ls, p, e);

	return 0;
}

/* add new element in head */
int string_list_add_head(struct string_list *ls, char *str)
{
	struct string_list_el *p;

	p = string_list_create_el(str);
	if (p == NULL)
		return VZT_CANT_ALLOC_MEM;
	TAILQ_INSERT_HEAD(ls, p, e);

	return 0;
}

/* remove all elements and its content */
void string_list_clean(struct string_list *ls)
{
	struct string_list_el *el;

	while (ls->tqh_first != NULL) {
		el = ls->tqh_first;
		TAILQ_REMOVE(ls, ls->tqh_first, e);
		free((void *)el->s);
		free((void *)el);
	}
}

/* find string <str> in list <ls> */
struct string_list_el *string_list_find(struct string_list *ls, char *str)
{
	struct string_list_el *p;

	if (str == NULL)
		return NULL;

	for (p = ls->tqh_first; p != NULL; p = p->e.tqe_next) {
		if (strcmp(str, p->s) == 0)
			return p;
	}
	return NULL;
}

/* remove element and its content and return pointer to previous elem */
struct string_list_el *string_list_remove(
		struct string_list *ls,
		struct string_list_el *el)
{
	/* get previous element */
	struct string_list_el *prev = *el->e.tqe_prev;

	TAILQ_REMOVE(ls, el, e);
	free((void *)el->s);
	free((void *)el);

	return prev;
}

/*
 read strings from file and add into list
 leading and tailing spaces omitted
 lines, commented by '#' ignored
*/
int string_list_read(const char *path, struct string_list *ls)
{
	int rc = 0;
	char str[STRSIZ];
	FILE *fp;
	char *sp;

	if (access(path, F_OK))
		return 0;

	if (!(fp = fopen(path, "r"))) {
		vztt_logger(0, errno, "Can not open %s", path);
		return VZT_CANT_OPEN;
	}

	while (fgets(str, sizeof(str), fp)) {
		if ((sp = cut_off_string(str)) == NULL)
			continue;

		if ((rc = string_list_add(ls, sp)))
			break;
	}
	fclose(fp);

	return rc;
}

/* the same as read_string_list and parse line, 
use space and tab as delimiter */
int string_list_read2(const char *path, struct string_list *ls)
{
	int rc = 0;
	char str[STRSIZ];
	FILE *fp;
	char *sp, *ep, *token;
	char *saveptr;

	if (access(path, F_OK))
		return 0;

	if (!(fp = fopen(path, "r"))) {
		vztt_logger(0, errno, "Can not open %s", path);
		return VZT_CANT_OPEN;
	}

	while (fgets(str, sizeof(str), fp)) {
		if ((sp = cut_off_string(str)) == NULL)
			continue;

		/* parse string */
		for (ep = sp; ;ep = NULL) {
			if ((token = strtok_r(ep, " 	", &saveptr)) == NULL)
				break;
			if ((rc = string_list_add(ls, token)))
				break;
		}
	}
	fclose(fp);

	return rc;
}

/* get size of string list <ls> */
size_t string_list_size(struct string_list *ls)
{
	struct string_list_el *p;
	size_t sz = 0;

	for (p = ls->tqh_first; p != NULL; p = p->e.tqe_next)
		sz++;
	return sz;
}

/* copy string list <ls> to string array <*a> */
int string_list_to_array(struct string_list *ls, char ***a)
{
	struct string_list_el *p;
	size_t sz, i;

	if (a == NULL)
		return 0;

	/* get array size */
	sz = string_list_size(ls);
	if ((*a = (char **)calloc(sz + 1, sizeof(char *))) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	for (p = ls->tqh_first, i = 0; p != NULL && i < sz; \
				p = p->e.tqe_next, i++) {
		if (((*a)[i] = strdup(p->s)) == NULL) {
			vztt_logger(0, errno, "Cannot alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	}
	(*a)[sz] = NULL;

	return 0;
}

/* copy all elements of <src> to <dst> */
int string_list_copy(struct string_list *dst, struct string_list *src)
{
	int rc;
	struct string_list_el *p;

	for (p = src->tqh_first; p != NULL; p = p->e.tqe_next)
		if ((rc = string_list_add(dst, p->s)))
			return rc;
	return 0;
}

/* sort all elements in <tosort> */
int string_list_sort(struct string_list *tosort)
{
	size_t size, entry;
	struct string_list_el *p;
	struct string_list_el *r;
	char *backup;

	if (string_list_empty(tosort))
		return 0;

	if ((size = string_list_size(tosort)) == 1)
		return 0;

	p = tosort->tqh_first;
	for (entry = 0; entry != size - 1; entry++)
	{
		r = p->e.tqe_next;
		if (strcmp(p->s, r->s) > 0)
		{
			backup = p->s;
			p->s = r->s;
			r->s = backup;
			p = tosort->tqh_first;
			entry = 0;
		}
		else
		{
			p = p->e.tqe_next;
		}
	}

	return 0;
}

/* TODO: clear tc->url_map */
/* add new element in tail */
int url_map_list_add(struct url_map_list *ls, char *src, char *dst)
{
	struct url_map_rec *u;

	u = (struct url_map_rec *)malloc(sizeof(struct url_map_rec));
	if (u == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	u->src = strdup(src);
	u->dst = strdup(dst);
	if (u->src == NULL || u->dst == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	TAILQ_INSERT_TAIL(ls, u, e);

	return 0;
}

/* clean url map list */
void url_map_list_clean(struct url_map_list *ls)
{
	struct url_map_rec *el;

	while (ls->tqh_first != NULL) {
		el = ls->tqh_first;
		TAILQ_REMOVE(ls, ls->tqh_first, e);
		if (el->src)
			free((void *)el->src);
		if (el->dst)
			free((void *)el->dst);
		free((void *)el);
	}
}


/* add new element in tail */
int repo_list_add(struct repo_list *ls, char *url, char *id, int num)
{
	struct repo_rec *r;

	r = (struct repo_rec *)malloc(sizeof(struct repo_rec));
	if (r == NULL)
		return vztt_error(VZT_SYSTEM, errno, "malloc()");
	r->url = strdup(url);
	r->id = strdup(id);
	r->num = num;
	if (r->url == NULL || r->id == NULL)
		return vztt_error(VZT_SYSTEM, errno, "strdup()");
	TAILQ_INSERT_TAIL(ls, r, e);

	return 0;
}

/*
 read strings from file and add into repo list
 leading and tailing spaces omitted
 lines, commented by '#' ignored
*/
int repo_list_read(char *path, char *id, int *num, struct repo_list *ls)
{
	int rc = 0;
	char str[STRSIZ];
	FILE *fp;
	char *sp;

	if (access(path, F_OK))
		return 0;

	if (!(fp = fopen(path, "r"))) {
		vztt_logger(0, errno, "Can not open %s", path);
		return VZT_CANT_OPEN;
	}

	while (fgets(str, sizeof(str), fp)) {
		if ((sp = cut_off_string(str)) == NULL)
			continue;

		if ((rc = repo_list_add(ls, sp, id, (*num)++)))
			break;
	}
	fclose(fp);

	return rc;
}

/* find record with url == <str> in list <ls> */
struct repo_rec *repo_list_find(struct repo_list *ls, char *url)
{
	struct repo_rec *p;

	for (p = ls->tqh_first; p != NULL; p = p->e.tqe_next) {
		if (strcmp(url, p->url) == 0)
			return p;
	}
	return NULL;
}

/* get size of repo list <ls> */
size_t repo_list_size(struct repo_list *ls)
{
	size_t sz = 0;
	struct repo_rec *p;

	for (p = ls->tqh_first; p != NULL; p = p->e.tqe_next)
		sz++;
	return sz;
}

/* clean repo list */
void repo_list_clean(struct repo_list *ls)
{
	struct repo_rec *el;

	while (ls->tqh_first != NULL) {
		el = ls->tqh_first;
		TAILQ_REMOVE(ls, ls->tqh_first, e);
		if (el->url)
			free((void *)el->url);
		if (el->id)
			free((void *)el->id);
		free((void *)el);
	}
}

/* compare two repo lists: 0 - equals, 1 - differs */
int repo_list_cmp(struct repo_list *ls0, struct repo_list *ls1)
{
	struct repo_rec *p0, *p1;
	/* compare repositories */
	for (	p0 = ls0->tqh_first, p1 = ls1->tqh_first;
		p0 && p1;
		p0 = p0->e.tqe_next, p1 = p1->e.tqe_next)
	{
		if (strcmp(p0->url, p1->url))
			return 1;
	}
	if (p0 || p1)
		return 1;
	return 0;
}

/* add new pkg_info element in tail of queue */
int pkg_info_list_add(struct pkg_info_list *ls, struct pkg_info *i)
{
	struct pkg_info_list_el *u;

	u = (struct pkg_info_list_el *)malloc(sizeof(struct pkg_info_list_el));
	if (u == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	u->i = i;
	TAILQ_INSERT_TAIL(ls, u, e);

	return 0;
}

/* remove all elements */
void pkg_info_list_clean(struct pkg_info_list *ls)
{
	struct pkg_info_list_el *el;

	while (ls->tqh_first != NULL) {
		el = ls->tqh_first;
		TAILQ_REMOVE(ls, ls->tqh_first, e);
		free((void *)el);
	}
}

/* get size of pkg_info list <ls> */
size_t pkg_info_list_size(struct pkg_info_list *ls)
{
	struct pkg_info_list_el *p;
	size_t sz = 0;

	for (p = ls->tqh_first; p != NULL; p = p->e.tqe_next)
		sz++;
	return sz;
}


/* copy pkg_info list <ls> to pkg_info array <*a>
   Attn: this function does not copy list element content (pkg_info *) */
int pkg_info_list_to_array(struct pkg_info_list *ls, struct pkg_info ***a)
{
	struct pkg_info_list_el *p;
	size_t sz, i;

	if (a == NULL)
		return 0;

	/* get array size */
	sz = pkg_info_list_size(ls);
	if ((*a = (struct pkg_info **)calloc(sz + 1, \
			sizeof(struct pkg_info *))) == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	for (p = ls->tqh_first, i = 0; \
			p != NULL && i < sz; p = p->e.tqe_next, i++)
		(*a)[i] = p->i;
	(*a)[sz] = NULL;

	return 0;
}

/* insert struct package <p> as element in tail of queue */
int package_list_insert(struct package_list *ls, struct package *p)
{
	struct package_list_el *u;

	u = (struct package_list_el *)malloc(sizeof(struct package_list_el));
	if (u == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	u->p = p;
	TAILQ_INSERT_TAIL(ls, u, e);

	return 0;
}

/* copy struct package <s> content and add it in tail of queue */
int package_list_add(struct package_list *ls, struct package *s)
{
	struct package *t;
	struct package_list_el *u;

	if ((t = create_structp(s->name, s->arch, s->evr, s->descr)) == NULL) {
		vztt_logger(0, errno, "Can't alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	u = (struct package_list_el *)malloc(sizeof(struct package_list_el));
	if (u == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	u->p = t;
	TAILQ_INSERT_TAIL(ls, u, e);

	return 0;
}

/* remove element and its content and return pointer to previous elem */
struct package_list_el *package_list_remove(
		struct package_list *ls,
		struct package_list_el *el)
{
	/* get previous element */
	struct package_list_el *prev = *el->e.tqe_prev;

	/* remove content */
	erase_structp(el->p);
	TAILQ_REMOVE(ls, el, e);
	free((void *)el);

	return prev;
}

/* remove all elements with content */
void package_list_clean(struct package_list *ls)
{
	struct package_list_el *el;

	while (ls->tqh_first != NULL) {
		el = ls->tqh_first;
		TAILQ_REMOVE(ls, ls->tqh_first, e);
		erase_structp(el->p);
		free((void *)el);
	}
}
/* get size of pkg_info list <ls> */
size_t package_list_size(struct package_list *ls)
{
	struct package_list_el *p;
	size_t sz = 0;

	for (p = ls->tqh_first; p != NULL; p = p->e.tqe_next)
		sz++;
	return sz;
}

/* find (struct package *) in list<struct package *> 
 for name and arch (if arch is defined) */
struct package_list_el *package_list_find(
		struct package_list *packages,
		struct package *pkg)
{
	struct package_list_el *i;

	for (i = packages->tqh_first; i != NULL; i = i->e.tqe_next)
		if (cmp_pkg(pkg, i->p) == 0)
			return i;
	return NULL;
}

/* dump packages list */
void package_list_dump(struct package_list *packages)
{
	struct package_list_el *i;

	for (i = packages->tqh_first; i != NULL; i = i->e.tqe_next)
		printf(" %-22s %-9s %s\n", \
			i->p->name, i->p->arch, i->p->evr);
}

/* copy package list <ls> with content to package array <*a> */
int package_list_to_array(struct package_list *ls, struct package ***a)
{
	struct package_list_el *p;
	size_t sz, i;

	if (a == NULL)
		return 0;

	/* get array size */
	sz = package_list_size(ls);
	if ((*a = (struct package **)calloc(sz + 1, sizeof(struct package *)))\
			 == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	for (p = ls->tqh_first, i = 0; p != NULL && i < sz; \
			p = p->e.tqe_next, i++) {
		if (((*a)[i] = create_structp(p->p->name, p->p->arch, \
				p->p->evr, p->p->descr)) == NULL) {
			vztt_logger(0, errno, "Can't alloc memory");
			return VZT_CANT_ALLOC_MEM;
		}
	}
	(*a)[sz] = NULL;

	return 0;
}

/* copy all elements of <src> to <dst> */
int package_list_copy(struct package_list *dst, struct package_list *src)
{
	int rc;
	struct package_list_el *p;

	for (p = src->tqh_first; p != NULL; p = p->e.tqe_next)
		if ((rc = package_list_add(dst, p->p)))
			return rc;
	return 0;
}


/* 
 unsigned double-linked list 
*/
/* add new element in tail */
int unsigned_list_add(struct unsigned_list *ls, unsigned u)
{
	struct unsigned_list_el *p;

	p = (struct unsigned_list_el *)malloc(sizeof(struct unsigned_list_el));
	if (p == NULL) {
		vztt_logger(0, errno, "Cannot alloc memory");
		return VZT_CANT_ALLOC_MEM;
	}
	p->u = u;
	TAILQ_INSERT_TAIL(ls, p, e);

	return 0;
}

/* remove all elements and its content */
void unsigned_list_clean(struct unsigned_list *ls)
{
	struct unsigned_list_el *el;

	while (ls->tqh_first != NULL) {
		el = ls->tqh_first;
		TAILQ_REMOVE(ls, ls->tqh_first, e);
		free((void *)el);
	}
}

/* get size of list <ls> */
size_t unsigned_list_size(struct unsigned_list *ls)
{
	struct unsigned_list_el *p;
	size_t sz = 0;

	for (p = ls->tqh_first; p != NULL; p = p->e.tqe_next)
		sz++;
	return sz;
}

