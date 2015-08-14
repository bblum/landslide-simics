#!/bin/bash

function msg {
	echo -e "\033[01;33m$1\033[00m"
}
function err {
	echo -e "\033[01;31m$1\033[00m" >&2
}
function die() {
	err "$1"
        exit 1
}

TEST_CASE=$1

if [ -z "$TEST_CASE" ]; then
	TEST_CASE="wait-simple"
	msg "$0: test case not specified; defaulting to $TEST_CASE"
fi
if [ ! -f kernel.bin ]; then
	die "can't $0 -- kernel.bin missing"
fi
if [ ! -f loader.bin ]; then
	die "can't $0 -- loader.bin missing"
fi

FILESYS=./filesys.dsk
if [ ! -f "$FILESYS" ]; then
	msg "Filesystem disk image ($FILESYS) missing. Building pintos without, but userspace won't work."
	FILESYS_ARG=""
else
	FILESYS_ARG="--filesys-from=$FILESYS"
fi

rm -f bootfd.img
./pintos/src/utils/pintos-mkdisk --kernel=kernel.bin --format=partitioned --loader=loader.bin $FILESYS_ARG bootfd.img -- run $TEST_CASE || die "couldn't $0 -- pintos-mkdisk failed"

#### Filesystem incantation:
#### Run this command on vipassana (or otherwise w/ bochs installation):
# cd ~/berkeley/group0/pintos/src/userprog/build
# ../../utils/pintos-mkdisk filesys.dsk --filesys-size=2
# ../../utils/pintos -p tests/userprog/wait-simple -a wait-simple -- -f -q
# scp filesys.dsk ...
# # TODO: put multiple programs
