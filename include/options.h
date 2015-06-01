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
 * vztt internal options structure declaration
 */

#ifndef __OPTIONS_H__
#define __OPTIONS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "vztt_types.h"
#include "vztt_options.h"

struct options_vztt *options_convert(struct options *opts);

/* opts->vztt field mask */
#define OPT_VZTT_NONE 0
#define OPT_VZTT_QUIET (1U << 0)
#define OPT_VZTT_FORCE (1U << 1)
#define OPT_VZTT_FORCE_SHARED (1U << 2)
#define	OPT_VZTT_DEPENDS (1U << 3)
#define	OPT_VZTT_TEST (1U << 4)
#define	OPT_VZTT_SKIP_LOCK (1U << 5)
#define	OPT_VZTT_CUSTOM_PKG (1U << 6)
#define	OPT_VZTT_VZ_DIR (1U << 7)
#define	OPT_VZTT_STDI_TYPE (1U << 8)
#define	OPT_VZTT_STDI_VERSION (1U << 9)
#define	OPT_VZTT_STDI_TECH (1U << 10)
#define	OPT_VZTT_STDI_FORMAT (1U << 11)
#define	OPT_VZTT_ONLY_STD (1U << 12)
#define	OPT_VZTT_WITH_STD (1U << 13)
#define	OPT_VZTT_CACHED_ONLY (1U << 14)
#define	OPT_VZTT_PKGID (1U << 15)
#define	OPT_VZTT_FORCE_OPENAT (1U << 16)
#define	OPT_VZTT_EXPANDED (1U << 17)
#define	OPT_VZTT_INTERACTIVE (1U << 18)
#define	OPT_VZTT_SEPARATE (1U << 19)
#define	OPT_VZTT_UPDATE_CACHE (1U << 20)
#define	OPT_VZTT_SKIP_DB (1U << 21)
#define	OPT_VZTT_USE_VZUP2DATE (1U << 22)
#define	OPT_VZTT_FORCE_VZCTL (1U << 23)
#define	OPT_VZTT_AVAILABLE (1U << 24)

#ifdef __cplusplus
}
#endif

#endif
