#!/bin/bash

VZTT=../vztt
LOGFILE=../vztt.tst.log
DLEVEL=2

echo -e "\n\nCheck package info"
$VZTT info -d $DLEVEL fedora-core-4-x86_64 mysql 2>&1 | tee -a $LOGFILE
[ ${PIPESTATUS[0]} -eq 0 ] || exit 1

echo -e "\n\nCheck package info"
$VZTT info -d $DLEVEL fedora-core-4-x86_64 mysql arch 2>&1 | tee -a $LOGFILE
[ ${PIPESTATUS[0]} -eq 0 ] || exit 1

echo -e "\nSuccess.\n"
