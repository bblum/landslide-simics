#                                                              -*- Makefile -*-
# Copyright (c) Virtutech AB, All Rights Reserved
#
# Simics module makefile
#

MODULE_CLASSES=landslide

SRC_FILES = landslide.c \
	    simics_glue.c \
	    x86.c \
	    kernel_specifics.c \
	    student.c \
	    user_specifics.c \
	    schedule.c \
	    arbiter.c \
	    save.c \
	    test.c \
	    explore.c \
	    estimate.c \
	    found_a_bug.c \
	    rbtree.c \
	    memory.c \
	    user_sync.c \
	    rand.c \
	    lockset.c \
	    vector_clock.c \
	    stack.c \
	    symtable.c \
	    messaging.c \
	    pp.c

MODULE_CFLAGS =

SIMICS_API := 4.0
THREAD_SAFE:=yes

ifeq ($(MODULE_MAKEFILE),)
$(error Make sure you compile your module from the workspace directory)
else
include $(MODULE_MAKEFILE)
endif
