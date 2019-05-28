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
 *  vztt error codes
 */

#ifndef _VZTT_ERROR_H_
#define _VZTT_ERROR_H_

// error codes
/* system error */
#define VZT_CANT_OPEN 			1
#define VZT_CANT_READ 			1
#define VZT_CANT_LSTAT 			1
#define VZT_CANT_FIND 			1
#define VZT_CANT_CHROOT	 		1
#define VZT_CANT_CHDIR	 		1
#define VZT_CANT_COPY			1
#define VZT_CANT_FORK	 		1
#define VZT_CANT_WRITE			1
#define VZT_CANT_REMOVE			1
#define VZT_CANT_CREATE			1
#define VZT_CANT_MMAP			1
#define VZT_CANT_RENAME			1
#define VZT_ATTR_ERR			1
#define VZT_EATTR_ERR			1
#define VZT_SYSTEM			1
/* internal error */
#define VZT_INTERNAL			2
/* command execution error */
#define VZT_CANT_EXEC	 		3
/* memory allocation error */
#define VZT_CANT_ALLOC_MEM		4
/* vzctl binary not found */
#define VZT_VZCTL_NFOUND		5
/* file already exist (for file copy) */
#define VZT_FILE_EXIST			6
/* VZ license not loaded, or invalid class ID */
#define VZT_NO_LICENSE			7
/* running command exit with non-null code */
#define VZT_CMD_FAILED			8
/* file or directory not found */
#define VZT_FILE_NFOUND			9
/* program run from non-root user */
#define VZT_USER_NOT_ROOT		10
/* Can not calculate md5sum */
#define VZT_CANT_CALC_MD5SUM		11
/* Object exist, but is not a directory */
#define VZT_NOT_DIR 			12
/* broken magic symlink found */
#define VZT_BROKEN_SLINK		13
/* can not parse string */
#define VZT_CANT_PARSE			14
/* can not fetch file */
#define VZT_CANT_FETCH			52
/* Can not get service status */
#define VZT_CANT_GET_VZ_STATUS		54
/* Service does not running */
#define VZT_VZ_NOT_RUNNING		55
/* Invalid URL */
#define VZT_INVALID_URL			56
/* Can not get existing VE list */
#define VZT_CANT_GET_VE_LIST		61
/* vzctl library error */
#define VZT_VZCTL_ERROR			62
/* program failed */
#define VZT_PROG_FAILED			63
/* program terminated by signal */
#define VZT_PROG_SIGNALED		64
/* repository metadata not found */
#define VZT_METADATA_NFOUND		67
/* can not lock VE or ostemplate */
#define VZT_CANT_LOCK			21
/* can not unlock VE or ostemplate */
#define VZT_CANT_UNLOCK			69
/* package manager failed */
#define VZT_PM_FAILED			70
/* ploop library error */
#define VZT_PLOOP_ERROR			72

/*
  VE errors
 */
/* VE does not running */
#define VZT_VE_NOT_RUNNING		15
/* VE does not exist */
#define VZT_VE_NOT_EXIST		16
/* VE does suspended */
#define VZT_VE_SUSPENDED		17
/* VE does not mounted */
#define VZT_VE_NOT_MOUNTED		18
/* VE does not suspended */
#define VZT_VE_NOT_SUSPENDED		66
/* VE has invalid status */
#define VZT_VE_INVALID_STATUS		19
/* can not lock free temporary ve for cache */
#define VZT_CANT_LOCK_FREE_VE		20
/* VE or ostemplate cache is not up2date */
#define VZT_NOT_UP2DATE			22

/* 
  Template errors
 */
/* template does not cached */
#define VZT_TMPL_NOT_CACHED		23
/* template does not found */
#define VZT_TMPL_NOT_FOUND		24
/* broken/invalid template */
#define VZT_TMPL_BROKEN			25
/* this template does not installed into ve */
#define VZT_TMPL_NOT_INSTALLED		26
/* this template installed into ve */
#define VZT_TMPL_INSTALLED		27
/* this template does not exist on HN */
#define VZT_TMPL_NOT_EXIST		28
/* template cache already exist */
#define VZT_TCACHE_EXIST		29
/* config for template cache not found */
#define VZT_TCACHE_CONF_NFOUND		30
/* init executable for cache creation not found */
#define VZT_TCACHE_INIT_NFOUND		31
/* base os template has extra os template(s) */
#define VZT_TMPL_HAS_EXTRA		32
/* base os template has application template(s) */
#define VZT_TMPL_HAS_APPS		65
/* this rpm is not ez template */
#define VZT_NOT_EZ_TEMPLATE		33
/* this rpm is not standard template */
#define VZT_NOT_STD_TEMPLATE		34
/* VZFS reserved */
//#define VZT_VZFS3_TMPL_AREA		53
/* unknown os template architecture */
#define VZT_TMPL_UNKNOWN_ARCH		57
/* unsupported os template architecture */
#define VZT_TMPL_UNSUPPORTED_ARCH	58
/* Template area resides on the shared partition */
#define VZT_TMPL_SHARED			68
// VZFS reserved
//#define VZT_TCACHE_NFS			71
/* Template doesn't support operatins with pkgs */
#define VZT_TMPL_PKGS_OPS_NOT_ALLOWED	73

/*
    Argument errors
 */
/* Bad argument */
#define VZT_BAD_PARAM			35
/* LOCKDIR variable is not defined in global config */
#define VZT_LOCKDIR_NOTSET		36
/* TEMPLATE variable is not defined in global config */
#define VZT_TEMPLATE_NOTSET		37
/* VE_ROOT variable is not defined in global and VE configs */
#define VZT_VE_ROOT_NOTSET		38
/* VE_PRIVATE variable is not defined in global and VE configs */
#define VZT_VE_PRIVATE_NOTSET		39
/* OSTEMPLATE variable is not defined in VE config */
#define VZT_VE_OSTEMPLATE_NOTSET	40
/* can not replace $VEID variable to value in VE_ROOT or VE_PRIVATE */
#define VZT_CANT_REPLACE_VEID		41
/* Can not get VZFORMAT from VE private VERSION link */
#define VZT_CANT_GET_VEFORMAT		42
/* This VEFORMAT does not supported by kernel */
#define VZT_UNSUPPORTED_VEFORMAT 	43
/* Unknown VEFORMAT in VERSION link in VE private */ 
#define VZT_UNKNOWN_VEFORMAT		44
/* Unknown package manage name */
#define VZT_UNKNOWN_PACKAGE_MANAGER 	45
/* bad VE name */
#define VZT_BAD_VE_NAME			46
/* Unknown technologies  */
#define VZT_UNKNOWN_TECHNOLOGY		47
/* Unsupported technologies  */
#define VZT_BAD_TECHNOLOGY		48
/* can not find environment directory */
#define VZT_ENVDIR_NFOUND		49
/* broken environment directory */
#define VZT_ENVDIR_BROKEN		50
/* can not find tempoarry directory */
#define VZT_TMPDIR_NFOUND		51
/* Can not get VE layout version */
#define VZT_CANT_GET_LAYOUT		59
/* Unknown VE layout version */
#define VZT_UNKNOWN_LAYOUT		60
/* Unsupported command */
#define VZT_UNSUPPORTED_COMMAND		61

#endif
