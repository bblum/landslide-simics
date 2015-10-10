#!/bin/sh

# adds a ghc cluster machine to pldi experiment swarm

function warn {
	echo -e "\033[01;33mwarning: $@\033[00m"
}

function die {
	echo -e "\033[01;31merror: $@\033[00m"
	exit 1
}

if [ "`grep MHz /proc/cpuinfo | wc -l`" != 12 ]; then
	die "bad cluster machine"
fi

if [ -d "/tmp/shit/masters" ]; then
	die "already in swarm"
fi

cd masters || die "fail cd masters"
git branch | grep '*' | grep dr-experimence >/dev/null || die "masters not on right git branch"

mkdir -p /tmp/shit || die "fail mkdir"
chmod go-rx /tmp/shit || die "fail chmod"
cd /tmp/shit || die "fail cd"

tar xvjf /afs/andrew.cmu.edu/usr12/bblum/p2s-fixed-new.tar.bz2 || die "fail untar p2s"

warn "getting repo"
cp -r /afs/andrew.cmu.edu/usr12/bblum/masters . || die "fail cp masters"
cd masters || die "fail cd"
./prepare-workspace.sh || die "fail prepare wksp"
cd work || die "fail cd"
make clean || warn "fail make clean"
make || warn "fail make"
cd ../id || die "fail cd id"
make || die "fail make id"
cd ../ || die "fail cd" # to /tmp/shit/masters
cd ../ || die "fail cd" # to /tmp/shit


warn "setting up control dirs"

for i in `seq 1 10`; do
	cp -r masters "control$i" || die "fail create control$i dir"
	cd "control$i" || die "fail cd"

	# copy pasta from above
	./prepare-workspace.sh || die "fail prepare wksp"
	cd work || die "fail cd"
	make clean || warn "fail make clean"
	make || warn "fail make"
	cd ../ || die "fail cd" # to /tmp/shit/control$i
	cd ../ || die "fail cd" # to /tmp/shit
done

echo -e "\033[01;32msuccess; ready to do some SCIENCE!\033[00m"
