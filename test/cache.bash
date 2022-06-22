#!/bin/bash

VZPKG=../src/vzpkg
LOGFILE=vztt.tst.log
DLEVEL=2

function cache_test()
{
	local ostemplate=$1

	# remove cache
	if compgen -G "/vz/template/cache/${ostemplate}*.lz4" > /dev/null; then
		$VZPKG remove cache -d $DLEVEL $ostemplate 2>&1 | tee -a $LOGFILE
		if [ ${PIPESTATUS[0]} -ne 0 ] ; then
			echo "remove cache $ostemplate error"
			exit 1
		fi
	fi
	# create cache
	$VZPKG create cache -d $DLEVEL $ostemplate 2>&1 | tee -a $LOGFILE
	if [ ${PIPESTATUS[0]} -ne 0 ] ; then
		echo "create cache $ostemplate error"
		exit 1
	fi
	# update cache
	$VZPKG update cache -d $DLEVEL $ostemplate 2>&1 | tee -a $LOGFILE
	if [ ${PIPESTATUS[0]} -ne 0 ] ; then
		echo "update cache $ostemplate error"
		exit 1
	fi
}

cache_test almalinux-8-x86_64

cache_test ubuntu-18.04-x86_64

cache_test centos-7-x86_64

echo -e "\nCache test success.\n"
