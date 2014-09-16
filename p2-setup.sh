#!/bin/bash

function msg() {
	echo -ne '\033[01;36m'
	echo "$1"
	echo -ne '\033[00m'
}

function die() {
	echo -ne '\033[01;31m'
	echo "$1"
	echo -ne '\033[00m'
	exit 1
}

# Some quick checks before we get started with side effects
if [ -z "$1" ]; then
	die "usage: ./setup.sh ABSOLUTE_PATH_TO_P2_DIRECTORY"
fi
if [ ! -d "$1" ]; then
	die "argument '$1' is not a directory"
fi

# Get started with side effects.

./prepare-workspace.sh || die "couldn't prepare workspace"

# Build iterative deepening wrapper.

cd id || die "couldn't cd into id"
make || die "couldn't build id program"

# Put config.landslide into place.

cd ../pebsim || die "couldn't cd into pebsim"

CONFIG=config.landslide.pathos-p2

[ -f $CONFIG ] || die "couldn't find appropriate config: $CONFIG"

rm -f config.landslide
ln -s $CONFIG config.landslide || die "couldn't create config symlink"

# Import and build student p2.

cd p2-basecode || die "couldn't cd into p2 basecode directory"

msg "Importing your p2 into '$PWD' - look there if something goes wrong..."

./import-p2.sh "$1" || die "could not import your p2"

PATH=/afs/cs/academic/class/15410-f14/bin/:$PATH make || die "import p2 was successful, but build failed (from '$PWD')"

cp bootfd.img ../../pebsim/ || die "couldn't move floppy disk image (from '$PWD')"
cp kernel ../../pebsim/ || die "couldn't move kernel binary (from '$PWD')"

echo -ne '\033[01;32m'
echo "Setup successful. Can now run ./landslide."
echo -ne '\033[00m'
