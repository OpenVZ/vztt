#!/bin/bash

VZPKG=../src/vzpkg
LOGFILE=vztt.tst.log
DLEVEL=2

function cache_test()
{
	local ostemplate=$1

	# remove cache
	if [ -f /vz/template/cache/${ostemplate}.tar.gz ] ; then
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

cache_test redhat-as4-x86

cache_test fedora-core-5-x86_64

cache_test suse-10.2-x86_64

cache_test debian-3.1-x86-addons

cache_test ubuntu-6.10-x86_64

cache_test ubuntu-6.06-x86

echo -e "\nCache test success.\n"
