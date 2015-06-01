#!/bin/bash

VZTT=../src/vzpkg
LOGFILE=../vztt.tst.log
DLEVEL=2

echo -e "\n\nCheck OS template all info"
$VZTT info -d $DLEVEL debian-3.1-x86 2>&1 | tee -a $LOGFILE
[ ${PIPESTATUS[0]} -eq 0 ] || exit 1

echo -e "\n\nCheck OS template info field"
$VZTT info -d $DLEVEL debian-3.1-x86 packages_0 2>&1 | tee -a $LOGFILE
[ ${PIPESTATUS[0]} -eq 0 ] || exit 1

echo -e "\n\nCheck app template all info"
$VZTT info -d $DLEVEL -F debian-3.1-x86 devel 2>&1 | tee -a $LOGFILE
[ ${PIPESTATUS[0]} -eq 0 ] || exit 1

echo -e "\n\nCheck OS template info field"
$VZTT info -d $DLEVEL -F debian-3.1-x86 devel packages 2>&1 | tee -a $LOGFILE
[ ${PIPESTATUS[0]} -eq 0 ] || exit 1

echo -e "\n\nCheck package info"
$VZTT info -d $DLEVEL -F debian-3.1-x86 -p mysql-common 2>&1 | tee -a $LOGFILE
[ ${PIPESTATUS[0]} -eq 0 ] || exit 1

echo -e "\n\nCheck package info"
$VZTT info -d $DLEVEL -F debian-3.1-x86 -p mysql-common arch 2>&1 | tee -a $LOGFILE
[ ${PIPESTATUS[0]} -eq 0 ] || exit 1

echo -e "\nSuccess.\n"

