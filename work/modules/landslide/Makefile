#                                                              -*- Makefile -*-
# Copyright (c) Virtutech AB, All Rights Reserved
#
# Simics module makefile
#

MODULE_CLASSES=landslide

SRC_FILES = landslide.h landslide.c \
	    simics_glue.c \
	    x86.c x86.h \
	    kspec.h \
	    kernel_specifics.h kernel_specifics.c student.c \
	    user_specifics.h user_specifics.c \
	    schedule.h schedule.c \
	    arbiter.h arbiter.c \
	    save.h save.c \
	    test.h test.c \
	    explore.h explore.c \
	    estimate.h estimate.c \
	    tree.h \
	    found_a_bug.h found_a_bug.c \
	    rbtree.h rbtree.c \
	    memory.h memory.c \
	    user_sync.h user_sync.c \
	    rand.h rand.c \
	    lockset.c lockset.h \
	    stack.c stack.h \
	    symtable.c symtable.h \
	    messaging.c messaging.h \
	    pp.c pp.h \
	    html.h \
	    array_list.h \
	    variable_queue.h

MODULE_CFLAGS =

SIMICS_API := strict
THREAD_SAFE:=yes

ifeq ($(MODULE_MAKEFILE),)
$(error Make sure you compile your module from the workspace directory)
else
include $(MODULE_MAKEFILE)
endif
