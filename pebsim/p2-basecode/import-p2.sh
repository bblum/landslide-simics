#!/bin/bash

DIR=$1

function die() {
	echo -ne '\033[01;31m'
	echo "$1"
	echo -ne '\033[00m'
	exit 1
}

function check_subdir() {
	if [ ! -d "$DIR/$1" ]; then
		die "$DIR doesn't look like a p2 directory -- $1 subdir missing (try creating it and try again)"
	fi
}

if [ -z "$DIR" ]; then
	die "usage: $0 <absolute-path-to-p2-directory>"
fi
if [ ! -d "$DIR" ]; then
	die "$DIR not a directory"
fi
check_subdir 410kern
check_subdir 410user
check_subdir spec
check_subdir user
check_subdir user/libthread
check_subdir user/libsyscall
check_subdir user/libautostack
check_subdir user/inc
# check_subdir vq_challenge

DESTDIR=p2-basecode
if [ "`basename $PWD`" != "$DESTDIR" ]; then
	die "$0 run from wrong directory -- need to cd into $DESTDIR, wherever that is"
fi

function sync_subdir() {
	mkdir -p "./$1"
	rsync -rlpvgoD --delete "$DIR/$1/" "./$1/" || die "rsync failed."
}
function sync_optional_subdir() {
	if [ -d "$DIR/$1" ]; then
		sync_subdir "$1"
	fi
}

# note: if you update this you need to update check-for...sh too
sync_optional_subdir vq_challenge
sync_subdir user/inc
sync_subdir user/libautostack
sync_subdir user/libsyscall
sync_subdir user/libthread

# Merge student config.mk targets into ours

CONFIG_MK_PATTERN="THREAD_OBJS\|SYSCALL_OBJS\|AUTOSTACK_OBJS"

rm -f config.mk
grep -v "$CONFIG_MK_PATTERN" config-incomplete.mk >> config.mk
# replace backslash-newlines so grep finds entire list
cat "$DIR/config.mk" | tr '\n' '@' | sed 's/\\@/ /g' | sed 's/@/\n/g' | grep "$CONFIG_MK_PATTERN" >> config.mk

echo "import p2 success; you may now make, hopefully"
