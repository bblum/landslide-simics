#!/bin/bash

DIR=$1

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

if [ -z "$DIR" ]; then
	die "usage: $0 <absolute-path-to-pintos-directory>"
fi
if [ ! -d "$DIR" ]; then
	die "$DIR not a directory"
fi
# look for src/.
if [ ! -d "$DIR/src/threads" ]; then
	# support either "foo/" or "foo/pintos-p1/" or "foo/p1/",
	# when "foo" is supplied as DIR.
	if [ -d "$DIR/pintos-p1/src/threads" ]; then
		DIR="$DIR/pintos-p1"
	elif [ -d "$DIR/p1/src/threads" ]; then
		DIR="$DIR/p1"
	else
		die "Couldn't find src/ subdirectory in '$DIR'"
	fi
fi
check_subdir src/threads
check_subdir src/userprog
check_subdir src/vm
check_subdir src/devices
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
# our basecode is missing some files / patches from the uchicago version.
sync_subdir src/devices
sync_subdir src/lib
sync_subdir src/misc
sync_subdir src/utils
sync_subdir src/tests
# And the makefile. Just clobber, applying patch will take care of rest.
sync_file src/Makefile.build
sync_file src/Make.config

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
