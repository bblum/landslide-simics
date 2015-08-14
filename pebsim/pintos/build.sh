#!/bin/bash

# Converts pintos source (with landslide annotations already applied) into a
# bootfd.img and kernel binary and puts them into pebsim/.

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


USERPROG=$1
if [ -z "$USERPROG" ]; then
	die "usage: $0 <userprog>"
elif [ "$USERPROG" = "0" ]; then
	PROJECT=threads
else
	PROJECT=userprog
fi

msg "20th century pintos, $PROJECT edition."

cd pintos/src/$PROJECT
make || die "failed make"
cd ../../../
if [ -f kernel.o ]; then
	mv kernel.o kernel.o.old
fi
if [ -f kernel.o.strip ]; then
	mv kernel.o.strip kernel.o.strip.old
fi
cp pintos/src/$PROJECT/build/kernel.o kernel.o || die "failed cp kernel.o"
./fix-symbols.sh kernel.o || die "failed fix symbols"

# Put stuff in the right place for make-bootfd script
cp pintos/src/$PROJECT/build/kernel.bin kernel.bin || die "failed cp kernel.bin"
cp pintos/src/$PROJECT/build/loader.bin loader.bin || die "failed cp loader.bin"

./make-bootfd.sh || die "couldn't make boot disk image"

msg "Pintos images built successfully."

# Put final product in parent pebsim directory
cp bootfd.img ../bootfd.img || die "failed cp bootfd.img"
cp kernel.o.strip ../kernel-pintos || die "failed cp kernel.o.strip"
rm -f ../kernel
ln -s kernel-pintos ../kernel || die "failed create kernel symlink"
