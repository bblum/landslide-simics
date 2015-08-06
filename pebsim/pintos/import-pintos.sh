#!/bin/bash

DIR=$1
USERPROG=$2

function msg {
	echo -e "\033[01;33m$1\033[00m"
}

function die() {
	echo -ne '\033[01;31m'
	echo "$1"
	echo -ne '\033[00m'
	exit 1
}

function check_file() {
	if [ ! -f "$DIR/$1" ]; then
		die "$DIR doesn't look like a pintos directory -- $1 file missing (try creating it and try again)"
	fi
}
function check_subdir() {
	if [ ! -d "$DIR/$1" ]; then
		die "$DIR doesn't look like a pintos directory -- $1 subdir missing (try creating it and try again)"
	fi
}

if [ -z "$DIR" -o -z "$USERPROG" ]; then
	die "usage: $0 <absolute-path-to-pintos-directory> <userprog>"
fi
if [ ! -d "$DIR" ]; then
	die "$DIR not a directory"
fi
# look for src/.
if [ "$USERPROG" = "0" ]; then
	PROJ="p1"
else
	PROJ="p2"
fi
if [ ! -d "$DIR/src/threads" ]; then
	# support either "foo/" or "foo/pintos-pX/" or "foo/pX/",
	# when "foo" is supplied as DIR.
	if [ -d "$DIR/pintos-$PROJ/src/threads" ]; then
		DIR="$DIR/pintos-$PROJ"
	elif [ -d "$DIR/$PROJ/src/threads" ]; then
		DIR="$DIR/$PROJ"
	else
		die "Couldn't find src/ subdirectory in '$DIR'"
	fi
fi
check_subdir src/threads
check_subdir src/userprog
check_subdir src/vm
check_subdir src/devices
check_subdir src/filesys
check_subdir src/lib
check_subdir src/misc
check_subdir src/utils
check_file src/Makefile.build

# we should be inside pebsim/pintos/, and furthermore,
# we should copy files into pebsim/pintos/pintos/.
DESTDIR=pintos
SUBDIR=pintos

if [ "`basename $PWD`" != "$DESTDIR" ]; then
	die "$0 run from wrong directory -- need to cd into $DESTDIR, wherever that is"
fi

# Set up basecode if not already done.
if [ ! -d "$SUBDIR" ]; then
	REPO=group0
	if [ ! -d "$REPO" ]; then
		git clone "https://github.com/Berkeley-CS162/$REPO.git" || die "Couldn't clone basecode repository."
	fi
	mv "$REPO/$SUBDIR" "$SUBDIR" || die "couldn't bring $SUBDIR out of repository"
fi

function sync_file() {
	cp "$DIR/$1" "./$SUBDIR/$1" || die "cp of $1 failed."
}
function sync_subdir() {
	mkdir -p "./$SUBDIR/$1"
	rsync -av --delete "$DIR/$1/" "./$SUBDIR/$1/" || die "rsync failed."
}
function sync_optional_subdir() {
	if [ -d "$DIR/$1" ]; then
		sync_subdir "$1"
	fi
}

sync_subdir src/threads
sync_subdir src/userprog
sync_subdir src/vm
sync_subdir src/filesys # some studence may add fs code helper functions
# our basecode is missing some files / patches from the uchicago version.
sync_subdir src/devices
sync_subdir src/lib
sync_subdir src/misc
sync_subdir src/utils
sync_subdir src/tests
# And the makefile. Just clobber, applying patch will take care of rest.
sync_file src/Makefile.build
sync_file src/Make.config

# Patch source codez.

function check_file() {
	if [ ! -f "$1" ]; then
		die "Where's $1?"
	fi
}

# Fix bug in Pintos.pm in berkeley versions of basecode.
# This can't go in the patch because it's conditional on version...
PINTOS_PM="./$SUBDIR/src/utils/Pintos.pm"
check_file "$PINTOS_PM"
if grep "source = 'FILE'" "$PINTOS_PM" >/dev/null; then
	sed -i "s/source = 'FILE'/source = 'file'/" "$PINTOS_PM" || die "couldn't fix $PINTOS_PM"
	msg "Successfully fixed $PINTOS_PM."
fi

# Fix CFLAGS and apply part of patch manually. Needs to be sed instead of patch
# to compensate for variance among studence implementations.
MAKE_CONFIG="./$SUBDIR/src/Make.config"
check_file "$MAKE_CONFIG"
if grep "^CFLAGS =" "$MAKE_CONFIG" >/dev/null; then
	sed -i "s/^CFLAGS =/CFLAGS = -fno-omit-frame-pointer/" "$MAKE_CONFIG" || die "couldn't fix CFLAGS in $MAKE_CONFIG"
else
	die "$MAKE_CONFIG doesn't contain CFLAGS?"
fi

THREAD_H="./$SUBDIR/src/threads/thread.h"
THREAD_C="./$SUBDIR/src/threads/thread.c"
check_file "$THREAD_H"
check_file "$THREAD_C"

echo "struct list *get_rq_addr() { return &ready_list; }" >> "$THREAD_C"
# It's ok for a function decl to go outside the ifdef.
echo "struct list *get_rq_addr(void);" >> "$THREAD_H"

# Apply tell_landslide annotations.

PATCH=annotate-pintos.patch
if [ ! -f "$PATCH" ]; then
	die "can't see annotations patch file, $PATCH; where is it?"
fi
# -l = ignore whitespace
# -f = force, don't ask questions
patch -l -f -p1 -i "$PATCH" || die "Failed to patch in annotations. You need to manually merge them. See patch output above."
# TODO - come up with some advice on what the user should do with the scripts if the patch fails to apply?

# success
echo "import pintos success; you may now make, hopefully"
