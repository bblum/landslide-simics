#!/bin/bash

function msg() {
	echo -ne '\033[01;33m'
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

# Process threads-vs-userprog argument
USERPROG=0
ARG2=$2
if [ -z "$2" ]; then
	ARG2=threads
fi
if [ "$ARG2" = "threads" -o "$ARG2" = "k" ]; then
	msg "Setting up a kernel-space pintos build"
elif [ "$ARG2" = "userprog" -o "$ARG2" = "u" ]; then
	msg "Setting up a user-space pintos build"
	USERPROG=1
else
	die "Argument 2 must be either 'threads' or 'userprog'"
fi

# Get started with side effects.

./prepare-workspace.sh || die "couldn't prepare workspace"

# Build iterative deepening wrapper.

cd id || die "couldn't cd into id"
make || die "couldn't build id program"

# Put config.landslide into place.

cd ../pebsim || die "couldn't cd into pebsim"

CONFIG=config.landslide.pintos
SYMLINK=config.landslide

[ -f $CONFIG ] || die "couldn't find appropriate config: $CONFIG"

rm -f "$SYMLINK"
ln -s "$CONFIG" "$SYMLINK" || die "couldn't create config symlink"

if grep "PINTOS_USERPROG" $CONFIG >/dev/null; then
	sed -i "s/^PINTOS_USERPROG.*/PINTOS_USERPROG=$USERPROG/" "$CONFIG" || die "couldn't set PINTOS_USERPROG setting in $SYMLINK"
else
	echo "PINTOS_USERPROG=$USERPROG" >> config.landslide || die "couldn't add PINTOS_USERPROG setting to config.landslide"
fi

# Import and build student pintos.

cd pintos || die "couldn't cd into pintos directory"

PINTOSDIR="$PWD"
msg "Importing your Pintos into '$PINTOSDIR' - look there if something goes wrong..."

# expect this to generate subdirectory called pintos/ inside of the pintos/
# we're already in, eg pintos/[WE ARE HERE]/pintos/src/threads
./import-pintos.sh "$1" "$USERPROG" || die "could not import your pintos"

./build.sh "$USERPROG" || die "import pintos was successful, but build failed (from '$PWD')"

## build.sh already does dis
#cp bootfd.img ../../pebsim/ || die "couldn't move floppy disk image (from '$PWD')"
#cp kernel ../../pebsim/ || die "couldn't move kernel binary (from '$PWD')"

cd ../../pebsim/ || die "couldn't cd into pebsim"

# Fix size of console lock in config, which varies across ze studence.
if grep "CONSOLE_LOCK_SIZE" $CONFIG >/dev/null; then
	# compute size of console lock
	CONSOLE_LOCK_NUMS=`objdump -t kernel | sort | grep -A1 '\<console_lock\>' | cut -d' ' -f1`
	CONSOLE_LOCK_BASE=`echo $CONSOLE_LOCK_NUMS | cut -d' ' -f1`
	CONSOLE_LOCK_END=`echo $CONSOLE_LOCK_NUMS | cut -d' ' -f2`
	CONSOLE_LOCK_SIZE=$((0x$CONSOLE_LOCK_END - 0x$CONSOLE_LOCK_BASE))
	msg "This kernel's locks are $CONSOLE_LOCK_SIZE bytes."
	sed -i "s/CONSOLE_LOCK_SIZE=.*/CONSOLE_LOCK_SIZE=$CONSOLE_LOCK_SIZE/" "$CONFIG" || die "couldn't sed console lock size"
fi

msg "Setting up Landslide..."

export LANDSLIDE_CONFIG=config.landslide
./build.sh || die "Failed to compile landslide. Please send a tarball of this directory to Ben for assistance."

echo -ne '\033[01;33m'
echo "Note: Your Pintos was imported into '$PINTOSDIR'. If you wish to make changes to it, recommend editing it in '$1' then running this script again."
echo -ne '\033[01;32m'
echo "Setup successful. Can now run ./landslide."
echo -ne '\033[00m'
rm -f current-pintos-group.txt
echo "$1" >> current-pintos-group.txt
