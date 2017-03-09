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
 */

#ifndef LIST_AVAIL_H
#define	LIST_AVAIL_H

#ifdef	__cplusplus
extern "C" {
#endif

#define FAKE_CONFDIR    "not-installed"

int list_avail_get_full_list(char ** ostemplates, struct tmpl_list_el ***ls, int mask);
int list_avail_get_list(char * ostemplate, struct tmpl_list_el ***ls, int mask);


#ifdef	__cplusplus
}
#endif

#endif	/* LIST_AVAIL_H */

