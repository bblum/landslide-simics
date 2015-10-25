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

TRY=live-delta-goodcputime

function runtest {
	echo -e "\033[01;32mTesting $semestername $groupname on $TEST_CASE\033[00m"
	group="$DIR/$semestername/$groupname"
	# verify the command to be run is correqt
	#echo "./p2-setup.sh $group >/dev/null;">> which-seat-should-i-take-live
	#echo "./landslide -v -p $TEST_CASE -c 10 -t3600 &" >> which-seat-should-i-take-live

	rm -f work/modules/landslide/student_specifics.h
	./p2-setup.sh $group >/dev/null;
	success=$?;
	if [ "$success" = "0" ]; then
		sed -i 's/DONT_EXPLORE=1/DONT_EXPLORE=0/' pebsim/config.landslide.pathos-p2
		SECS=3600 # 1hrs
		GRACE=120 # 2 extra min for idk, whatever
		INCR=60
		CORES=10
		TIMESPLEPT=0
		./landslide -v -p $TEST_CASE -c ${CORES} -t${SECS} &

		# frickin afs; frickin bootfd.img truncated read
		for increment in `seq $INCR $INCR $(($GRACE+$SECS))`; do
			TIMESPLEPT=$(($TIMESPLEPT+$INCR))
			sleep $INCR
			if [ "`pgrep landslide | wc -l`" = "0" ]; then
				break
			fi
		done
		echo "Slept for $TIMESPLEPT secs; issuing kill signals"
		killall landslide
		anylskilled=$?
		killall landslide-id
		anylsidkilled=$?
		killall simics-common
		anysimkilled=$?

		LOGDIR="$HOME/masters/p2-id-logs/test-$TEST_CASE-try$TRY"
		DESTDIR="$LOGDIR/$TEST_CASE/$semestername-$groupname"
		mkdir -p "$DESTDIR"
		for logpath in `ls id/ls-*log* | grep -v '\.gz$' | grep -v afsfull`; do
			gzip "$logpath"
			mv "$logpath".gz "$DESTDIR"
			rv=$?
			if [ "$rv" != "0" ]; then
				echo "warning, AFS seems to be full. this log belonged to $semestername $groupname running $TEST_CASE."
				touch "${logpath}-afsfull-belongsto-$semestername-$groupname-$TEST_CASE"
			fi
		done

		# try warn for possible rerun (if not too many of these) if any landslide hang bugs
		if [ "$anylskilled" = "0" ]; then
			touch "$DESTDIR"/lskilled
		fi
		if [ "$anylsidkilled" = "0" ]; then
			touch "$DESTDIR"/lsidkilled
		fi
		if [ "$anysimkilled" = "0" ]; then
			touch "$DESTDIR"/simkilled
		fi
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
		aklog

		semestername=`echo "$line" | cut -d' ' -f1`
		groupname=`echo "$line" | cut -d' ' -f2`
		TEST_CASE=`echo "$line" | cut -d' ' -f3`
		runtest
	done
}

myjobfile=`pwd`/../livejobfile.txt

numjobs=`cat $myjobfile | wc -l`
echo "will run jobs from $myjobfile ($numjobs jobs)"

cat $myjobfile | runjobs
