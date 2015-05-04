#!/bin/sh

# Simics throws an assertion if I try to deflsym.load-symbols directly on the
# kernel.o that pintos's build process outputs. Instead, this...

KERNEL="$1"

if [ ! -f "$KERNEL" ]; then
	echo no kernel
	exit 1
fi

ALLSYMS_FILE=`mktemp pintos-allsyms.XXXXXXXX`

if [ ! -f "$ALLSYMS_FILE" ]; then
	echo failed mktemp
	exit 1
fi


objdump -t "$KERNEL" | tail -n+5 | head -n-2 | sed 's/.*\t//' | cut -d' ' -f2- > $ALLSYMS_FILE

objcopy --strip-unneeded --keep-symbols="$ALLSYMS_FILE" "$KERNEL" "${KERNEL}.strip"
NUM=`wc -l $ALLSYMS_FILE | cut -d' ' -f1`
rm -f $ALLSYMS_FILE
echo "stripped all but $NUM symbols into ${KERNEL}.strip"
