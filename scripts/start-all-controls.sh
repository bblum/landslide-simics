#!/bin/sh

for i in `seq 1 10`; do
	if [ ! -d "control$i" ]; then
		echo "control$i dir missing"
		exit 1
	fi
done
if [ ! -d "p2s" ]; then
	echo "p2s missing"
	exit 1
fi

machinename=`hostname | cut -d. -f1`

jobsfile=grupos.txt

myjobs=`grep $machinename $jobsfile`

for i in `seq 1 10`; do
	controljobfile=controljobfile-${i}.txt
	jobs=`echo "$myjobs" | grep "$i $machinename$" | cut -d' ' -f-3`
	echo -ne '\033[01;32m'
	echo "control experiment #$i; with `echo "$jobs" | wc -l` jobs"
	echo -ne '\033[01;00m'
	rm -f $controljobfile
	echo "$jobs" >> $controljobfile
	if [ -z "$jobs" ]; then
		echo "skipping cpu $i, no jobs"
		continue;
	fi

	cd "control$i"
	# attempt to avert "COMPLETE (0 interleavings)" bug??
	sleep 5
	screen -S "control$i" -d -m ../run-control-experiment.sh
	cd ../
done

