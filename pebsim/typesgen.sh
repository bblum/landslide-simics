#!/bin/sh

# The way to use this script is as follows:
# On stdin, say e.g. "@begin struct pcb"
# Then paste, one per line, the members of it - the C code should be fine;
# Legal line examples: "int tid;" "pcb *parent", "char stack[...]"
# Illegal line examples: "int tid, pid;"
# Then type @end whatever
# Repeat for as many struct types as you wish to instrument.

KERNEL_NAME=$1

if [ -z "$KERNEL_NAME" ]; then
	echo "what is the name of this kernel?"
	exit 1
fi

KERNEL_NAME_LOWER=`echo $KERNEL_NAME | tr '[:upper:]' '[:lower:]'`
KERNEL_NAME_UPPER=`echo $KERNEL_NAME | tr '[:lower:]' '[:upper:]'`

echo "/**"
echo " * @file typesgen_$KERNEL_NAME_LOWER.c"
echo " * @brief generates a kernel_specifics_types.h file for $KERNEL_NAME"
echo " * @author Ben Blum <bblum@andrew.cmu.edu>"
echo " */"

echo
echo "#include <stdio.h>"
echo "#include <assert.h>"
echo "#error \"Please manually replace this line with the right #includes.\""
echo
echo "/* Suggest building this file with something like:"
echo " *     gcc -m32 -Iinc -I../410kern -I../spec typesgen_$KERNEL_NAME_LOWER.c"
echo " * or:"
echo " *     gcc -m32 -Ikern/inc -I410kern -Ispec typesgen_$KERNEL_NAME_LOWER.c"
echo " */"

echo
echo "int main()"
echo "{"
echo -e "\tlong member_size, member_offset, total_size;"

echo -e "\tprintf(\"/**\\\\n\");"
echo -e "\tprintf(\" * @file kernel_specifics_types_$KERNEL_NAME.c\\\\n\");"
echo -e "\tprintf(\" * @brief struct type information for $KERNEL_NAME (automatically generated)\\\\n\");"
echo -e "\tprintf(\" * @author Ben Blum <bblum@andrew.cmu.edu>\\\\n\");"
echo -e "\tprintf(\" */\\\\n\");"
echo -e "\tprintf(\"\\\\n\");"
echo -e "\tprintf(\"#ifndef __LS_KERNEL_SPECIFICS_TYPES_${KERNEL_NAME_UPPER}_H\\\\n\");"
echo -e "\tprintf(\"#define __LS_KERNEL_SPECIFICS_TYPES_${KERNEL_NAME_UPPER}_H\\\\n\");"
echo -e "\tprintf(\"\\\\n\");"
echo -e "\tprintf(\"struct struct_member {\\\\n\");"
echo -e "\tprintf(\"\\tint size;\\\\n\");"
echo -e "\tprintf(\"\\tint offset;\\\\n\");"
echo -e "\tprintf(\"\\tconst char *name;\\\\n\");"
echo -e "\tprintf(\"};\\\\n\");"
echo -e "\tprintf(\"\\\\n\");"

TYPE=""

while read LINE; do 
	if [ "`echo $LINE | cut -d' ' -f1`" == "@begin" ]; then
		if [ ! -z "$TYPE" ]; then
			echo "Tried to \"$LINE\", but TYPE was already $TYPE!"
			exit 1
		elif ! (echo $LINE | grep ' ' 2>&1 >/dev/null); then
			echo "Need another token after @begin: \"$LINE\""
			exit 1
		fi
		TYPE=`echo $LINE | cut -d' ' -f2-`
		if [ -z "$TYPE" ]; then
			echo "You think you're so funny? \"$LINE\""
			exit 1
		fi
		VARNAME="my_`echo $TYPE | sed 's/ /_/g'`"
		# Declare a local variable of the type we are doing.
		echo -e "\t$TYPE $VARNAME;"
		# Emit the beginning of the array to be building.
		echo -e "\tprintf(\"static const struct struct_member guest_$TYPE[] = {\\\\n\");"
		echo -e "\ttotal_size = 0;"
		# Process next input.
		continue;
	elif [ "`echo $LINE | cut -d' ' -f1`" == "@end" ]; then
		if [ -z "$TYPE" ]; then
			echo "Can't end type; none currently active!"
			exit 1
		fi
		# Reset TYPE to empty and emit an end for the array we built.
		echo -e "\tassert(member_offset + member_size == sizeof($VARNAME) &&"
		echo -e "\t       total_size == sizeof($VARNAME) &&"
		echo -e "\t       \"The field sizes don't add up!\");"
		echo -e "\tprintf(\"};\\\\n\");"
		echo -e "\tprintf(\"#define GUEST_`echo $TYPE | sed 's/ /_/g' | tr '[:lower:]' '[:upper:]'`_SIZE %d\\\\n\", total_size);"
		echo -e "\tprintf(\"\\\\n\");"
		TYPE=""
		# Process next input.
		continue;
	elif [ -z "$TYPE" ]; then
		echo "Can't deal with line $LINE, while TYPE unspecified!"
		exit 1
	fi

	# Then the line should be a pasted line that's a struct member.
	if ! (echo $LINE | grep ';' 2>&1 >/dev/null); then
		echo "Got $LINE, but expected a line ending in \';\'..."
		exit 1
	elif (echo $LINE | grep ',' 2>&1 >/dev/null); then
		echo "Got $LINE, and struct member lines with commas are not supported"
		exit 1
	fi
	# Get the name of the field from the line.
	# The first set strips out array stuff (e.g. "[4096]")
	# The cut strips the semi at the end
	# The second sed strips pointer indirections (e.g. "struct tcb *child")
	# The third sed strips leading type declarations ("const struct foo blah")
	MEMBERNAME=`echo $LINE | sed 's/\[.*\]//g' | cut -d';' -f1 | sed 's/.*\*//' | sed 's/.* //'`
	# Sanity check: should be exactly one token.
	if ! (echo $MEMBERNAME | grep '\<.*\>' 2>&1 >/dev/null); then
		echo "Somehow got member $MEMBERNAME that wasn't a token!"
		exit 1
	elif (echo $MEMBERNAME | grep '\<.*\>.*\<.*\>' 2>&1 >/dev/null); then
		echo "Got member $MEMBERNAME that is multiple tokens; shoulda been just one."
		exit 1
	fi

	# Emit a member for the guest struct for this member.
	echo -e "\tmember_offset = (char *)(&$VARNAME.$MEMBERNAME) - (char *)(&$VARNAME);"
	echo -e "\tmember_size = sizeof($VARNAME.$MEMBERNAME);"
	echo -e "\ttotal_size += member_size;"
	echo -e "\tprintf(\"\\\\t{ .size = %ld, .offset = %ld, .name = \\\"$MEMBERNAME\\\" },\\\\n\", member_size, member_offset);"
done

echo -e "\tprintf(\"\\\\n\");"
echo -e "\tprintf(\"#endif\\\\n\");"
echo -e "\treturn 0;"
echo -e "}"
