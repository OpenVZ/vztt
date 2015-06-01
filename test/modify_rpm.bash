#!/bin/bash

VZPKG=../src/vzpkg
VEID=5050
IPADDR=10.0.78.202
LOGFILE=vztt.tst.log
DLEVEL=2

if vzctl status $VEID | grep -q exist ; then
	#[ $? -eq 0 ] && { echo "VE $VEID already exist"; exit 1; }
	vzctl stop $VEID
	vzctl destroy $VEID
fi

vzctl create $VEID --ostemplate fedora-core-5-x86_64 2>&1 | tee -a $LOGFILE
[ ${PIPESTATUS[0]} -eq 0 ] || exit 1
vzctl set $VEID --ipadd $IPADDR --hostname ${VEID}.tst.sw.ru --userpass root:1q2w3e --save 2>&1 | tee -a $LOGFILE
[ ${PIPESTATUS[0]} -eq 0 ] || exit 1
vzctl start $VEID 2>&1 | tee -a $LOGFILE
[ ${PIPESTATUS[0]} -eq 0 ] || exit 1

# check simple template installation
$VZPKG install -d $DLEVEL $VEID mysql 2>&1 | tee -a $LOGFILE
[ ${PIPESTATUS[0]} -eq 0 ] || exit 1
vzpkg list $VEID | grep mysql
[ $? -eq 0 ] || { echo "point 1"; exit 1; }

# check implicit template removing
# without -w/-f
$VZPKG remove -d $DLEVEL -p $VEID mysql mysql-devel 2>&1 | tee -a $LOGFILE
[ ${PIPESTATUS[0]} -ne 0 ] || exit 1
vzpkg list $VEID | grep mysql
[ $? -eq 0 ] || { echo "point 2"; exit 1; }

# with -f
$VZPKG remove -d $DLEVEL -p -f $VEID mysql mysql-devel 2>&1 | tee -a $LOGFILE
[ ${PIPESTATUS[0]} -eq 0 ] || exit 1
vzpkg list $VEID | grep mysql
[ $? -ne 0 ] || { echo "point 3"; exit 1; }

# check implicit template installation
$VZPKG install -d $DLEVEL -p $VEID mysql mysql-devel 2>&1 | tee -a $LOGFILE
[ ${PIPESTATUS[0]} -eq 0 ] || exit 1
vzpkg list $VEID | grep mysql
[ $? -eq 0 ] || { echo "point 4"; exit 1; }

# check simple template removing
$VZPKG remove -d $DLEVEL $VEID mysql 2>&1 | tee -a $LOGFILE
[ ${PIPESTATUS[0]} -eq 0 ] || exit 1
vzpkg list $VEID | grep mysql
[ $? -ne 0 ] || { echo "point 5"; exit 1; }

# localinstall 
$VZPKG localinstall -d $DLEVEL $VEID $PWD/mod_ssl-2.2.0-5.1.2.x86_64.rpm $PWD/mysql-5.0.18-2.1.x86_64.rpm 2>&1 | tee -a $LOGFILE
[ ${PIPESTATUS[0]} -eq 0 ] || exit 1
vzpkg list $VEID | grep mod_ssl
[ $? -eq 0 ] || { echo "point 6"; exit 1; }

echo -e "\nSuccess.\n"
