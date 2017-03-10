/*
 * Copyright (c) 2015-2017, Parallels International GmbH
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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include "vzcommon.h"
#include "config.h"
#include "vztt_error.h"
#include "util.h"
#include "tmplset.h"
#include "list_avail.h"

#define ATF_UNKNOWN     0
#define ATF_OS          1
#define ATF_APP         2
#define ATF_FAKE        4
#define ATF_FAKE_OS     (ATF_OS | ATF_FAKE)

TAILQ_HEAD(atemplate_list, atemplate_entry);

struct atemplate_entry {
	int flag;
	char * name;
	/* Version actually holds both version-release */
	char * version;
	/*
	    char * release;
	 */
	char * repository;
	struct atemplate_list * apps;
	TAILQ_ENTRY(atemplate_entry) e;
};

struct atemplate_entry * new_atemplate_entry(char * name, char * version,
	char * repository)
{
	struct atemplate_entry *newt = malloc(sizeof(struct atemplate_entry));

	if (!newt)
		return NULL;

	newt->flag = ATF_UNKNOWN;
	newt->name = strdup(name);
	newt->version = strdup(version);
	newt->repository = strdup(repository);
	newt->apps = NULL;

	return newt;
}

static int free_available_list(struct atemplate_list * alist);

static int free_atemplate_entry(struct atemplate_entry * ae)
{
	if (ae->apps) {
		free_available_list(ae->apps);
		free(ae->apps);
	}
	free(ae->repository);
	free(ae->version);
	free(ae->name);
	free(ae);

	return 0;
}

static int free_available_list(struct atemplate_list * alist)
{
	struct atemplate_entry * os;

	TAILQ_FOREACH(os, alist, e)
	free_atemplate_entry(os);

	return 0;
}

static int get_available_list(char * ostemplate, struct atemplate_list * alist)
{
	FILE * f;
	char buffer[132];
	char cmd[80];

	sprintf(cmd, "/usr/bin/yum list available '*%s-ez.noarch' 2>/dev/null",
		ostemplate ? ostemplate : "");
	f = popen(cmd, "r");

	if (!f)
		return 1;

	TAILQ_INIT(alist);

	while (fgets(buffer, sizeof(buffer), f)) {
		char name[80], version[80] = {0,}, repository[80];
		char * p;
		struct atemplate_entry * newt;
		int ret;

		ret = sscanf(buffer, "%s %s %s", name, version, repository);

		if (ret < 1)
			continue;

		if ((p = strstr(name, "-ez.noarch")) != NULL)
			*p = 0;
		else
			continue;

		newt = new_atemplate_entry(name, version, repository);
		TAILQ_INSERT_TAIL(alist, newt, e);
	}
	pclose(f);
	return 0;
}

static int parse_applications(struct atemplate_list * alist)
{
	struct atemplate_entry * os, *ap;

	TAILQ_FOREACH(os, alist, e)
	{
		for (ap = TAILQ_FIRST(alist); ap != NULL;) {
			struct atemplate_entry * ee = ap;
			char * p;
			ap = TAILQ_NEXT(ap, e);
			if (ee == os)
				continue;
			if (!((p = strstr(ee->name, os->name)) != NULL &&
				strlen(p) == strlen(os->name) &&
				p > ee->name &&
				*(p - 1) == '-'))
				continue;
			*(p - 1) = 0;
			os->flag |= ATF_OS;
			TAILQ_REMOVE(alist, ee, e);
			if (!os->apps) {
				os->apps = malloc(sizeof(struct atemplate_list));
				TAILQ_INIT(os->apps);
			}
			TAILQ_INSERT_TAIL(os->apps, ee, e);
		}
	}
	return 0;
}

static int list_avail_get(char ** ostemplates, struct tmpl_list_el ***ls, int mask, int full_list)
{
	size_t sz = 0;
	struct atemplate_list al;
	struct atemplate_entry * ae, *aae;
	int i, found, rc;

	get_available_list((ostemplates && !full_list &&
		(ostemplates[0] != NULL && ostemplates[1] == NULL)) ?
		ostemplates[0] : NULL, &al);

	/* Insert FAKE OS templates records, if it is not-available (i.e. already installed) */
	for (i = 0; ostemplates[i]; i++) {
		found = 0;
		TAILQ_FOREACH(ae, &al, e)
		if (strcmp(ae->name, ostemplates[i]) == 0)
			found = 1;

		if (!found) {
			ae = new_atemplate_entry(ostemplates[i], "", "");
			ae->flag = ATF_FAKE_OS;
			TAILQ_INSERT_HEAD(&al, ae, e);
		}
	}

	parse_applications(&al);

	/* Count number of templates */
	TAILQ_FOREACH(ae, &al, e)
	{
		if (mask != OPT_TMPL_APP)
			sz++;
		if (ae->apps && (mask != OPT_TMPL_OS))
			TAILQ_FOREACH(aae, ae->apps, e)
			sz++;
	}

	/* alloc & init array */
	if ((rc = tmplset_alloc_list(sz, ls)))
		return rc;
	i = 0;

	TAILQ_FOREACH(ae, &al, e)
	{
		if (mask != OPT_TMPL_APP) {
			((*ls)[i])->is_os = 1;
			((*ls)[i])->timestamp = strdup(ae->repository);
			((*ls)[i])->info->name = strdup(ae->name);
			if (ae->flag & ATF_FAKE)
				((*ls)[i])->info->confdir = strdup(FAKE_CONFDIR);
			i++;
		}
		if (ae->apps && (mask != OPT_TMPL_OS))
			TAILQ_FOREACH(aae, ae->apps, e) {
			((*ls)[i])->is_os = 0;
			((*ls)[i])->timestamp = strdup(aae->repository);
			((*ls)[i])->info->name = strdup(aae->name);
			i++;
		}
	}

	free_available_list(&al);

	return 0;
}

int list_avail_get_list(char * ostemplate, struct tmpl_list_el ***ls, int mask)
{
	char * ostemplates[2] = {NULL, NULL};
	ostemplates[0] = ostemplate;
	return list_avail_get(ostemplates, ls, mask, 0);
}

int list_avail_get_full_list(char ** ostemplates, struct tmpl_list_el ***ls, int mask)
{
	return list_avail_get(ostemplates, ls, mask, 1);
}
