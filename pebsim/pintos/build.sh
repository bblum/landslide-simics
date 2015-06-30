#!/bin/bash

# Converts pintos source (with landslide annotations already applied) into a
# bootfd.img and kernel binary and puts them into pebsim/.

function die() {
	echo "error: $1"
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
./pintos/src/utils/pintos-mkdisk --kernel=pintos/src/$PROJECT/build/kernel.bin --format=partitioned --loader=pintos/src/$PROJECT/build/loader.bin bootfd.img -- run $TEST_CASE || die "failed pintos mkdisk"

cp bootfd.img ../bootfd.img
cp kernel.o.strip ../kernel-pintos
rm -f ../kernel
ln -s kernel-pintos ../kernel
