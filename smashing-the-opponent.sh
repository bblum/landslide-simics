#!/bin/sh

## @file smashing-the-opponent.sh
## @brief Given a landslide-arbiter-choice-formatted directory, finds a series
##        of choices that leads to an unexplored state.
## @author Ben Blum <bblum@andrew.cmu.edu>
##
## A so formatted directory represents a single "choice point" in the execution
## of a test case. It has the following contents:
##   1. "eip" (file) - contains the eip at which the choice occurs
##   2. "tids" (file) - contains the tids choosable at this location.
##   3. Any number of either:
##        a) <tid> (directory) - subdirectory representing the next choice point
##           reached by choosing now a given tid
##        b) <tid> (file) - denotes that choosing a given tid causes the test
##           case to finish and pass (i.e., the guest does not crash). (if this
##           causes the test to fail, there is no reason to make this file.)
##      The list of tid files and dirs must be a subset of the "tids" list.
##   4. "done" (file) (optional) - indicates all subtrees have already been
##      explored.

SELF=$0
DIR=$1

## 0 - consistency check

function consistency {
	if [ ! -d "$1" ]; then
		echo "error: $1 not a directory"
		exit 1
	fi

	if [ ! -f "$1/eip" -o ! -f "$1/tids" ]; then
		echo "error: missing required files in $1"
		exit 1
	fi

	if [ -f "$1/done" ]; then
		echo "error: already fully explored!"
		exit 1
	fi
}

## 1 - propagate doneness

function propagate {
	consistency $1
	for i in `cat "$1/tids"`; do
		if [ -f "$1/$i" ]; then
			continue;
		elif [ -d "$1/$i" ]; then
			if [ -f "$1/$i/done" ]; then
				continue;
			fi
			propagate "$1/$i"
			if [ -f "$1/$i/done" ]; then
				continue;
			fi
			# directory was not done
			return
		fi
		# file or directory doesn't even exist yet
		return
	done
	# all options done
	touch "$1/done"
}

propagate $DIR

## 2 - find a choice.

# TODO: ordering, partial reduction -.-

function choices {
	for i in `cat "$1/tids"`; do
		# skip file tids (end case)
		if [ -f "$1/$i" ]; then
			continue;
		elif [ -d "$1/$i" ]; then
			# skip 'done' directories
			if [ -f "$1/$i/done" ]; then
				continue;
			fi
			# found an incomplete directory - descend into it
			echo $i
			choices "$1/$i"
			return
		fi
		# found a totally unexplored tid - try it
		echo $i
		return
	done
	echo "error: somehow no options left in $1"
	exit 1
}

choices $DIR
exit 0
