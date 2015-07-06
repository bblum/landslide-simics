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

FILESYS=./filesys.dsk
if [ ! -f "$FILESYS" ]; then
	msg "Filesystem disk image ($FILESYS) missing. Building pintos without, but userspace won't work."
	FILESYS_ARG=""
else
	FILESYS_ARG="--filesys-from=$FILESYS"
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
rm -f bootfd.img
# TODO: change run priority-sema into run something else (parameterize)
TEST_CASE="wait-simple"
./pintos/src/utils/pintos-mkdisk --kernel=pintos/src/$PROJECT/build/kernel.bin --format=partitioned --loader=pintos/src/$PROJECT/build/loader.bin $FILESYS_ARG bootfd.img -- run $TEST_CASE || die "failed pintos mkdisk"

#### Filesystem incantation:
#### Run this command on vipassana (or otherwise w/ bochs installation):
# cd ~/berkeley/group0/pintos/src/userprog/build
# ../../utils/pintos-mkdisk filesys.dsk --filesys-size=2
# ../../utils/pintos -p tests/userprog/wait-simple -a wait-simple -- -f -q
# scp filesys.dsk ...
# # TODO: put multiple programs

cp bootfd.img ../bootfd.img || die "failed cp bootfd.img"
cp kernel.o.strip ../kernel-pintos || die "failed cp kernel.o.strip"
rm -f ../kernel
ln -s kernel-pintos ../kernel || die "failed create kernel symlink"
