#!/bin/bash

if [ ! -d work -o ! -d pebsim -o ! -d id ]; then
        echo "not a landslide repository?"
        exit 1
fi

function die() {
	echo "error $1"
	exit 1
}

sed -i "s:SIMICS_WORKSPACE=.*:SIMICS_WORKSPACE=$PWD/work:" work/config.mk || die 1
sed -i "s:add-module-directory.*:add-module-directory $PWD/work/linux64/lib:" pebsim/config.simics || die 2
sed -i "s:SIMENV=/.*:SIMENV=$PWD/pebsim:" pebsim/simics46 || die 3
if grep -R "bblum/masters" work >/dev/null; then
	grep -R "bblum/masters" work/ | cut -d: -f1 | xargs sed -i "s@/afs/andrew.cmu.edu/usr12/bblum/masters@$PWD@" || die 4
fi

echo "$0: success."
exit 0
