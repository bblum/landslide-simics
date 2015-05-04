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
	die "usage: ./setup.sh ABSOLUTE_PATH_TO_PINTOS_DIRECTORY"
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

CONFIG=config.landslide.pintos

[ -f $CONFIG ] || die "couldn't find appropriate config: $CONFIG"

rm -f config.landslide
ln -s $CONFIG config.landslide || die "couldn't create config symlink"

# Import and build student pintos.

cd pintos || die "couldn't cd into pintos directory"

PINTOSDIR="$PWD"
msg "Importing your Pintos into '$PINTOSDIR' - look there if something goes wrong..."

# expect this to generate subdirectory called pintos/ inside of the pintos/
# we're already in, eg pintos/[WE ARE HERE]/pintos/src/threads
./import-pintos.sh "$1" || die "could not import your pintos"

./build.sh || die "import pintos was successful, but build failed (from '$PWD')"

## build.sh already does dis
#cp bootfd.img ../../pebsim/ || die "couldn't move floppy disk image (from '$PWD')"
#cp kernel ../../pebsim/ || die "couldn't move kernel binary (from '$PWD')"

cd ../../pebsim/ || die "couldn't cd into pebsim"

msg "Setting up Landslide..."

export LANDSLIDE_CONFIG=config.landslide
./build.sh || die "Failed to compile landslide. Please send a tarball of this directory to Ben for assistance."

echo -ne '\033[01;33m'
echo "Note: Your Pintos was imported into '$PINTOSDIR'. If you wish to make changes to it, recommend editing it in '$1' then running this script again."
echo -ne '\033[01;32m'
echo "Setup successful. Can now run ./landslide."
echo -ne '\033[00m'
