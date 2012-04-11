#!/bin/sh

if [ "$(basename `pwd`)" != "landslide" ]; then
	echo "current directory must be named landslide."
	exit 1
fi
if [ ! -d work -o ! -d pebsim ]; then
	echo "not a landslide repository?"
	exit 1
fi

function verify_gone {
	if find | grep $1 2>/dev/null; then
		echo "get rid of $1"
		exit 1
	fi
}
verify_gone pobbles
verify_gone ludicros
verify_gone git

function check_workspace_script {
	if grep REPLACEME $1; then
		echo "not all directory paths fixed in $1"
		exit 1
	fi
	if grep usr12/bblum $1; then
		echo "frob directory paths in $1"
		exit 1
	fi
}
check_workspace_script work/config.mk
check_workspace_script pebsim/simics4
check_workspace_script pebsim/config.simics

rm $0
(cd .. && tar cvjf landslide.tar.bz2 landslide && scp landslide.tar.bz2 unix.andrew.cmu.edu:410/Web)
