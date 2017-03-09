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
 * Our contact details: Parallels IP Holdings GmbH, Vordergasse 59, 8200
 * Schaffhausen, Switzerland.
 *
 * Lock function declaration
 */

#include "vzcommon.h"
#include "config.h"
#include "template.h"

#ifndef _VZTT_LOCK_H_
#define _VZTT_LOCK_H_

#ifdef __cplusplus
extern "C" {
#endif
#define LOCK_READ  0
#define LOCK_WRITE 1

/* lock base os template */
int tmpl_lock(
		struct global_config *gc,
		struct base_os_tmpl *tmpl,
		int mode,
		int skiplock,
		void **lockdata);

/* unlock base os template */
int tmpl_unlock(void *lockdata, int skiplock);

/* lock template cache */
int cache_lock(
		struct global_config *gc,
		const char *cache_path,
		int mode,
		int vztt,
		void **lockdata);

/* unlock template cache */
int cache_unlock(void *lockdata, int vztt);

int lock_ve(const char *ctid, int vztt, void **lockdata);
void unlock_ve(const char *ctid, void *lockdata, int skiplock);

/* Locks for veid used in cache and appcache */
#define FREE_VEID_MIN   10000000
int lock_free_veid(int vztt, ctid_t ctid, void **lockdata);

#ifdef __cplusplus
}
#endif

#endif

