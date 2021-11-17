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
 * ploop operations module
 */

int convert_ploop(const char *old_cache, char *cache,
		struct options_vztt *opts_vztt);

int create_ploop(char *ploop_dir, unsigned long long diskspace_kb, struct options_vztt *opts_vztt);

int mount_ploop(char *ploop_dir, char *to, struct options_vztt *opts_vztt);

int umount_ploop(char *ploop_dir, struct options_vztt *opts_vztt);

int pack_ploop(char *ploop_dir, char *to_file, struct options_vztt *opts_vztt);

int resize_ploop(char *ploop_dir, struct options_vztt *opts_vztt, unsigned long long size);

int create_ploop_cache(
	const char *ctid, char *ostemplate, struct global_config *gc, struct vztt_config *tc,
	char *tmpdir,
	struct options_vztt *opts_vztt);

int create_ploop_dir(char *ve_private, const char *vefstype, char **ploop_dir);
