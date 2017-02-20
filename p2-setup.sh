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

VERSION_FILE=current-git-commit.txt
rm -f "$VERSION_FILE"
git show | head -n 1 > "$VERSION_FILE"

# Build iterative deepening wrapper.

cd id || die "couldn't cd into id"
make || die "couldn't build id program"

# Put config.landslide into place.

cd ../pebsim || die "couldn't cd into pebsim"

rm -f current-p2-group.txt
echo "$1" > current-p2-group.txt
rm -f current-architecture.txt
echo "p2" > current-architecture.txt

CONFIG=config.landslide.pathos-p2

[ -f $CONFIG ] || die "couldn't find appropriate config: $CONFIG"

rm -f config.landslide
ln -s $CONFIG config.landslide || die "couldn't create config symlink"

# Import and build student p2.

cd p2-basecode || die "couldn't cd into p2 basecode directory"

P2DIR="$PWD"
msg "Importing your p2 into '$P2DIR' - look there if something goes wrong..."

./import-p2.sh "$1" || die "could not import your p2"

PATH=/afs/cs.cmu.edu/academic/class/15410-s17/bin/:$PATH make || die "import p2 was successful, but build failed (from '$PWD')"

cp bootfd.img ../../pebsim/ || die "couldn't move floppy disk image (from '$PWD')"
cp kernel ../../pebsim/ || die "couldn't move kernel binary (from '$PWD')"

cd ../../pebsim/ || die "couldn't cd into pebsim"

msg "Setting up Landslide..."

rm -f ../work/modules/landslide/student_specifics.h
export LANDSLIDE_CONFIG=config.landslide
./build.sh || die "Failed to compile landslide. Please send a tarball of this directory to Ben for assistance."

echo -ne '\033[01;33m'
echo "Note: Your P2 was imported into '$P2DIR'. If you wish to make changes to it, recommend editing it in '$1' then running this script again."
echo -ne '\033[01;32m'
echo "Setup successful. Can now run ./landslide."
echo -ne '\033[00m'
