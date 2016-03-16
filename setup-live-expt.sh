#!/bin/sh

# adds a ghc cluster machine to pldi experiment swarm

function warn {
	echo -e "\033[01;33mwarning: $@\033[00m"
}

function die {
	echo -e "\033[01;31merror: $@\033[00m"
	exit 1
}

#if [ "`grep MHz /proc/cpuinfo | wc -l`" != 12 ]; then
#	die "bad cluster machine"
#fi

if [ ! -d "/tmp/shit/masters" ]; then
	die "not already in swarm"
fi

cd $HOME/masters || die "fail cd masters"
git branch | grep '*' | grep dr-experimence >/dev/null || die "masters not on right git branch"

cd /tmp/shit || die "fail cd"

warn "refreshing p2s"
rm -rf /tmp/shit/p2s
tar xvjf /afs/andrew.cmu.edu/usr12/bblum/p2s-even-newer-now.tar.bz2 || die "fail untar p2s"

warn "refreshing pintoses"
rm -rf /tmp/shit/pintoses
tar xvjf /afs/andrew.cmu.edu/usr12/bblum/pintoses-bothunis-try3-alarmtest.tar.bz2 || die "fail untar pintoses"

warn "getting repo"
rm -rf masters
cp -r /afs/andrew.cmu.edu/usr12/bblum/masters . || die "fail cp masters"

cp -r masters dr-falsenegs || die "fail make drfalsenegs"

warn "setting up masters"

cd masters || die "fail cd"
./prepare-workspace.sh || die "fail prepare wksp"
cd work || die "fail cd"
make clean || warn "fail make clean"
make || warn "fail make"
cd ../id || die "fail cd id"
sed -i 's/summarize_pending =/summarize_pending = true ||/' work.c || die "fail sed work.c" # avoid not-quite-quadratic blowup
make || die "fail make id"
cd ../ || die "fail cd" # to /tmp/shit/masters
cd ../ || die "fail cd" # to /tmp/shit

warn "setting up dr-falsenegs"

cd dr-falsenegs || die "fail cd"
./prepare-workspace.sh || die "fail prepare wksp"

DEFINESTR="#define DR_FALSE_NEGATIVE_EXPERIMENT"
sed -i "s@//$DEFINESTR@$DEFINESTR@" id/messaging.c || die "fail enable dr define in id"
sed -i "s@//$DEFINESTR@$DEFINESTR@" work/modules/landslide/memory.c || die "fail enable dr define in ls"

cd work || die "fail cd"
make clean || warn "fail make clean"
make || warn "fail make"
cd ../id || die "fail cd id"
sed -i 's/summarize_pending =/summarize_pending = true ||/' work.c || die "fail sed work.c" # avoid not-quite-quadratic blowup
make || die "fail make id"
cd ../ || die "fail cd" # to /tmp/shit/masters
cd ../ || die "fail cd" # to /tmp/shit

warn "getting scripce"

cd /tmp/shit || die "wat"
cp $HOME/masters/run-control-experiment.sh ./ || die "can't get runcontrol"
cp $HOME/masters/run-live-experiment.sh ./ || die "can't get runlive"
cp $HOME/masters/run-dr-experiment.sh ./ || die "can't get rundr"
cp $HOME/masters/start-all-controls.sh ./ || die "can't get startctrl"
cp $HOME/masters/start-live-expt.sh ./ || die "can't get startlive"
cp $HOME/masters/start-dr-expt.sh ./ || die "can't get startdr"
cp $HOME/masters/pintos-run-control-experiment.sh ./ || die "can't get pintos runctrl"
cp $HOME/masters/pintos-start-live-expt.sh ./ || die "can't get pintos start live"
cp $HOME/masters/pintos-run-live-experiment.sh ./ || die "can't get pintos run live"
cp $HOME/masters/pintos-start-all-controls.sh ./ || die "can't get pintos startctrl"


echo -e "\033[01;32msuccess; ready to do some SCIENCE!\033[00m"
