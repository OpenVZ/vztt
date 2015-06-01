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
 * vztt progress messages
 */

#ifndef _PROGRESS_MESSAGES_H_
#define _PROGRESS_MESSAGES_H_

/* cleanup.c */
#define PROGRESS_CLEAN_AREA "Cleaning template area"
#define PROGRESS_CLEAN_CACHE "Cleaning local cache"

/* modify.c */
#define PROGRESS_MODIFY "Modifying"
#define PROGRESS_REMOVE_PACKAGES "Removing packages"
#define PROGRESS_INSTALL_APPTEMPLATES "Installing application templates:"
#define PROGRESS_UPDATE_APPTEMPLATES "Updating application templates:"
#define PROGRESS_REMOVE_APPTEMPLATES "Removing application templates:"
#define PROGRESS_LOCALINSTALL "Installing packages"
#define PROGRESS_LOCALUPDATE "Updating packages"

/* misc.c */
#define PROGRESS_UP2DATE_STATUS "Checking for updates"
#define PROGRESS_GET_CACHE_STATUS "Checking cache status"
#define PROGRESS_REPAIR "Repairing template area"
#define PROGRESS_LINK "Linking packages"
#define PROGRESS_SYNC_PACKAGES "Synchronizing packages"
#define PROGRESS_FETCH_PACKAGES "Fetching packages"
#define PROGRESS_FETCH_TEMPLATE "Fetching the template %s"
#define PROGRESS_INSTALL_TEMPLATE "Installing template"
#define PROGRESS_UPDATE_TEMPLATE "Updating template"
#define PROGRESS_REMOVE_OSTEMPLATE "Removing the OS template %s"
#define PROGRESS_REMOVE_APPTEMPLATE "Removing the application template %s"
#define PROGRESS_UPGRADE_AREA "Upgrading OS template area"

/* info.c */
#define PROGRESS_GET_PACKAGE_INFO "Getting package information"
#define PROGRESS_GET_APPTEMPLATE_INFO "Getting application template information"
#define PROGRESS_GET_OSTEMPLATE_INFO "Getting OS template information"
#define PROGRESS_GET_GROUP_INFO "Getting package group information"

/* zypper.c, apt.c, yum.c */
#define PROGRESS_PKGMAN_PACKAGE_MANAGER "Package manager: "
#define PROGRESS_PKGMAN_DIST_UPGRADE "upgrading distribution"
#define PROGRESS_PKGMAN_FETCH "fetching"
#define PROGRESS_PKGMAN_INSTALL "installing"
#define PROGRESS_PKGMAN_REMOVE "removing"
#define PROGRESS_PKGMAN_LIST "listing"
#define PROGRESS_PKGMAN_CLEAN_METADATA "cleaning metadata"
#define PROGRESS_PKGMAN_MAKE_CACHE "making cache"
#define PROGRESS_PKGMAN_CLEAN "cleaning"
#define PROGRESS_PKGMAN_INFO "getting information"
#define PROGRESS_PKGMAN_UPGRADE "upgrading"
#define PROGRESS_PKGMAN_UPDATE "updating"
#define PROGRESS_PKGMAN_AVAILABLE "listing available items"
#define PROGRESS_PKGMAN_DOWNLOAD_PACKAGES "Downloading packages"
#define PROGRESS_PKGMAN_INST_PACKAGES "Installing packages"
#define PROGRESS_PKGMAN_INST_PACKAGES1 "Installing packages: stage 1"
#define PROGRESS_PKGMAN_INST_PACKAGES2 "Installing packages: stage 2"
#define PROGRESS_PKGMAN_INST_PACKAGES3 "Installing packages: stage 3"
#define PROGRESS_PKGMAN_GROUP_INSTALL "installing package group"
#define PROGRESS_PKGMAN_GROUP_UPDATE "updating package group"
#define PROGRESS_PKGMAN_GROUP_REMOVE "removing package group"

/* cache.c, appcache.c */
#define PROGRESS_CREATE_CACHE "Creating cache"
#define PROGRESS_CREATE_TEMP_CONTAINER "Creating temporary Container"
#define PROGRESS_RESTART_CONTAINER "Restarting Container"
#define PROGRESS_PACK_CACHE "Packing cache"
#define PROGRESS_UPDATE_CACHE "Updating cache"
#define PROGRESS_REMOVE_CACHE "Removing cache"
#define PROGRESS_CREATE_APPCACHE "Creating application template cache"
#define PROGRESS_UPDATE_APPCACHE "Updating application template cache"
#define PROGRESS_REMOVE_APPCACHE "Removing application template cache"

/* show_list.c */
#define PROGRESS_GET_CUSTOM_PACKAGES "Getting custom packages"
#define PROGRESS_GET_CONTAINER_PACKAGES "Getting Container packages"
#define PROGRESS_GET_CONTAINER_GROUPS "Getting Container package groups"
#define PROGRESS_GET_PACKAGE_DIRS "Getting package directories"
#define PROGRESS_GET_TEMPLATE_PKGS "Getting template packages"
#define PROGRESS_GET_TEMPLATE_GROUPS "Getting template groups"

/* util.c */
#define PROGRESS_RUN_SCRIPT "Running the script %s"

/* upgrade.c */
#define PROGRESS_UPGRADE "Upgrading"

/* ploop.c */
#define PROGRESS_CREATE_PLOOP "Creating virtual disk"
#define PROGRESS_RESIZE_PLOOP "Resizing virtual disk"

/* env_compat.c */
#define PROGRESS_PROCESS_METADATA "Processing metadata for %s"

#endif
