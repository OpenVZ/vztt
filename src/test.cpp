/*
 * Copyright (c) 2015 Parallels IP Holdings GmbH
 *
 * This file is part of OpenVZ. OpenVZ is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation;
 * either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * Our contact details: Parallels IP Holdings GmbH, Vordergasse 59, 8200
 * Schaffhausen, Switzerland.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vz/vztt.h>

/*
g++ -Wall test.cpp -Wl,-Bstatic -lvztt -lcurl -lssl -lcrypto \
-lgssapi_krb5 -lkrb5 -lz -lidn -lk5crypto -lkrb5support -luuid \
-Wl,-Bdynamic -lpthread -lslang -lresolv -lcom_err -lvzctl -o vztest
*/

int main(int argc, char *argv[])
{
	struct options opts;
	int rc, i;
	char **vzdir;
	unsigned veid;
	char *ostemplate;

	if (argc < 2)
		return 1;

	if ((veid = atoi(argv[1])) == 0)
		return 1;

	/* initialization */
	memset((void *)&opts, 0, sizeof(opts));

	/* get ve os template */
	if ((rc = vztt_get_ve_ostemplate(veid, &ostemplate)))
		return rc;

	/* get directory list */ 
	if ((rc = vztt_get_vzdir(veid, &opts, &vzdir)))
		return rc;

	/* lock ostemplate */
	if ((rc = vztt_lock_ostemplate(ostemplate)))
		return rc;

	for (i = 0; vzdir[i]; i++)
		printf("%s\n", vzdir[i]);

	/* unlock ostemplate */
	vztt_unlock_ostemplate(ostemplate);

	return 0;
}

