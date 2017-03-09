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
 * dpkg/apt wrapper module declarations
 */

#ifndef _APT_H_
#define _APT_H_

#include "transaction.h"

#define APT_GET_BIN	"/usr/bin/apt-get"
#define APT_CACHE_BIN	"/usr/bin/apt-cache"
#define DPKG_BIN	"/usr/bin/dpkg"
#define DPKG_QUERY_BIN	"/usr/bin/dpkg-query"
#define DPKG_DEB_BIN	"/usr/bin/dpkg-deb"

#define DPKG_DESCRIPTION_LEN 55

#ifdef __cplusplus
extern "C" {
#endif

struct AptTransaction
{
STRUCT_TRANSACTION_CONTENT
	char *apt_conf;
	char *sources;
	char *preferences;
	int download_only;
	struct string_list dpkg_options;
};

/* creation */
int apt_create(struct Transaction **pm);
#ifdef __cplusplus
}
#endif

#endif
