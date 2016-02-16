#!/bin/sh


DIR=/tmp/shit/p2s/

if [ "`ls id/ls-*log* | grep -v '\.gz$' | wc -l`" != "0" ]; then
	echo "found some id log files; get rid of that crap"
	exit 1
fi

MACHINE_CPUS=`grep MHz /proc/cpuinfo | wc -l`
if [ "$(($MACHINE_CPUS<10))" = "1" ]; then
	echo "err: $MACHINE_CPUS is not enough cpus. you will screw up your experimence."
	exit 1
fi

TRY=icb-rho

#for TEST_CASE in mutex_test thr_exit_join; do

function runtest {
	echo -e "\033[01;32mTesting $semestername $groupname on $TEST_CASE\033[00m"
	group="$DIR/$semestername/$groupname"
	# verify the command to be run is correqt
	#echo "./p2-setup.sh $group >/dev/null;">> which-seat-should-i-take-control$whichcontrolami
	#echo "./landslide -v -C -p $TEST_CASE -c 1 -t${SECS} &" >> which-seat-should-i-take-control$whichcontrolami

	rm -f work/modules/landslide/student_specifics.h
	./p2-setup.sh $group >/dev/null;
	success=$?;
	if [ "$success" = "0" ]; then
		sed -i 's/DONT_EXPLORE=1/DONT_EXPLORE=0/' pebsim/config.landslide.pathos-p2
		SECS=36000 # 10hrs
		GRACE=120 # 2 extra min for idk, whatever
		INCR=60
		CORES=10
		TIMESPLEPT=0
		./landslide -v -C -p $TEST_CASE -c 1 -t${SECS}
		# ICB
		#./landslide -v -C -I -p $TEST_CASE -c 1 -t${SECS}

		LOGDIR="$HOME/masters/p2-id-logs/test-$TEST_CASE-try$TRY"
		DESTDIR="$LOGDIR/$TEST_CASE/$semestername-$groupname"
		mkdir -p "$DESTDIR"
		for logpath in `ls id/ls-output*log* | grep -v '\.gz$' | grep -v afsfull`; do
			if ! head -n 1000 "$logpath" | grep License "$logpath" >/dev/null; then
				rm "$logpath"
			fi
		done
		for logpath in `ls id/ls-*log* | grep -v '\.gz$' | grep -v afsfull`; do
			gzip "$logpath"
			mv "$logpath".gz "$DESTDIR"
			rv=$?
			if [ "$rv" != "0" ]; then
				echo "warning, AFS seems to be full. this log belonged to $semestername $groupname running $TEST_CASE."
				touch "${logpath}-afsfull-belongsto-$semestername-$groupname-$TEST_CASE"
			fi
		done
		echo "Logs copied into $DESTDIR; moving on"
	else
		LOGDIR="$HOME/masters/p2-id-logs/test-$TEST_CASE-try$TRY"
		DESTDIR="$LOGDIR/$TEST_CASE/$semestername-$groupname"
		mkdir -p "$DESTDIR"
		touch "$DESTDIR/p2-setup-failed-RIP"
	fi
}

function runjobs {
	while read line; do
		kinit -R

		semestername=`echo "$line" | cut -d' ' -f1`
		groupname=`echo "$line" | cut -d' ' -f2`
		TEST_CASE=`echo "$line" | cut -d' ' -f3`
		runtest
		# attempt avert 0-interleavings-complete bug even on subsequent runs
		sleep $whichcontrolami
		sleep $whichcontrolami
		sleep $whichcontrolami
		sleep $whichcontrolami
	done
}

whichcontrolami=`basename $(pwd) | sed 's/control//'`
myjobfile=`pwd`/../controljobfile-${whichcontrolami}.txt

numjobs=`cat $myjobfile | wc -l`
echo "will run jobs from $myjobfile ($numjobs jobs)"

cat $myjobfile | runjobs
