#!/bin/sh

if [ ! -d work -o ! -d pebsim ]; then
        echo "not a landslide repository?"
        exit 1
fi


sed -i "s:SIMICS_WORKSPACE=.*:SIMICS_WORKSPACE=$PWD/work:" work/config.mk
sed -i "s:add-module-directory.*:add-module-directory $PWD/work/amd64-linux/lib:" pebsim/config.simics
sed -i "s:SIMENV=/.*:SIMENV=$PWD/pebsim:" pebsim/simics4
