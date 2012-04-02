#                                                              -*- Makefile -*-
# Copyright (c) Virtutech AB, All Rights Reserved
#
# Simics module makefile
#

# USER-TODO: specify the class(es) implemented in this module
MODULE_CLASSES=landslide

# USER-TODO: set file-names
SRC_FILES = landslide.h landslide.c \
	    x86.c x86.h \
	    kernel_specifics.h kernel_specifics.c student.c \
	    schedule.h schedule.c \
	    arbiter.h arbiter.c \
	    save.h save.c \
	    test.h test.c \
	    explore.h explore.c \
	    tree.h \
	    found_a_bug.h found_a_bug.c \
	    rbtree.h rbtree.c \
	    memory.h memory.c \
	    variable_queue.h

MODULE_CFLAGS =

SIMICS_API := strict
THREAD_SAFE:=yes

ifeq ($(MODULE_MAKEFILE),)
$(error Make sure you compile your module from the workspace directory)
else
include $(MODULE_MAKEFILE)
endif
