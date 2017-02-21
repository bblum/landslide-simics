#!/bin/bash

function die {
	echo -e "\033[01;31m$@\033[00m"
	exit 1
}

for i in `seq 1 10`; do
	if [ ! -d "control$i" ]; then
		die "missing control$i"
	fi
done
if [ ! -d "p2s" ]; then
	die "missing p2s"
fi
if [ ! -d "masters" ]; then
	die "missing masters"
fi

cp "$HOME/masters/run-control-experiment.sh" ./ || die "failed cp run ctrl sh"
cp "$HOME/masters/start-all-controls.sh" ./ || die "failed cp start ctrl sh"
cp "$HOME/masters/grupos-missing.txt" ./grupos.txt || die "failed cp jobs assignment file"

./start-all-controls.sh

