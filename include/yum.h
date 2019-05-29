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
 * Yum wrapper declaration
 */

#ifndef _YUM_H_
#define _YUM_H_

#include "transaction.h"

#define VZYUM_BIN	"/usr/share/vzyum/bin/yum"
#define VZYUMDB_PATH	"/var/lib/vzyum"

#ifdef __cplusplus
extern "C" {
#endif

struct YumTransaction
{
STRUCT_TRANSACTION_CONTENT
	char *yum_conf;
	char *pythonpath;
//	char *rpm;
};

/* create structure */
int yum_create(struct Transaction **pm);
#ifdef __cplusplus
}
#endif

#endif
