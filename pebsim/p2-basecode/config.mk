###########################################################################
#
#    #####          #######         #######         ######            ###
#   #     #         #     #            #            #     #           ###
#   #               #     #            #            #     #           ###
#    #####          #     #            #            ######             #
#         #         #     #            #            #
#   #     #         #     #            #            #                 ###
#    #####          #######            #            #                 ###
#
#
# Please read the directions in README and in this config.mk carefully.
# Do -N-O-T- just dump things randomly in here until your project builds.
# If you do that, you run an excellent chance of turning in something
# which can't be graded.  If you think the build infrastructure is
# somehow restricting you from doing something you need to do, contact
# the course staff--don't just hit it with a hammer and move on.
#
# [Once you've read this message, please edit it out of your config.mk!!]
###########################################################################



###########################################################################
# This is the include file for the make file.
###########################################################################
# You should have to edit only this file to get things to build.
#

###########################################################################
# Tab stops
###########################################################################
# If you use tabstops set to something other than the international
# standard of eight characters, this is your opportunity to inform
# our print scripts.
TABSTOP = 4

###########################################################################
# The method for acquiring project updates.
###########################################################################
# This should be "afs" for any Andrew machine, "web" for non-andrew machines
# and "offline" for machines with no network access.
#
# "offline" is strongly not recommended as you may miss important project
# updates.
#
UPDATE_METHOD = offline

###########################################################################
# WARNING: Do not put extraneous test programs into the REQPROGS variables.
#          Doing so will put your grader in a bad mood which is likely to
#          make him or her less forgiving for other issues.
###########################################################################

###########################################################################
# Mandatory programs whose source is provided by course staff
###########################################################################
# A list of the programs in 410user/progs which are provided in source
# form and NECESSARY FOR THE KERNEL TO RUN
#
# The idle process is a really good thing to keep here.
#
410REQPROGS = idle

###########################################################################
# Mandatory programs provided in binary form by course staff
###########################################################################
# A list of the programs in 410user/progs which are provided in binary
# form and NECESSARY FOR THE KERNEL TO RUN
#
# You may move these to 410BINPROGS to test your syscall stubs once
# they exist, but we will grade you against the binary versions.
# This should be fine.
#
410REQBINPROGS = shell init

###########################################################################
# WARNING: When we test your code, the two TESTS variables below will be
# ignored.  Your kernel MUST RUN WITHOUT THEM.
###########################################################################

###########################################################################
# Test programs provided by course staff you wish to run
###########################################################################
# A list of the test programs you want compiled in from the 410user/progs
# directory
#
410TESTS = thr_join_exit thr_exit_join paraguay rwlock_downgrade_read_test broadcast_test

###########################################################################
# Test programs you have written which you wish to run
###########################################################################
# A list of the test programs you want compiled in from the user/progs
# directory
#
STUDENTTESTS = odd_even_sort4_c1

###########################################################################
# Object files for your thread library
###########################################################################

# Thread Group Library Support.
#
# Since libthrgrp.a depends on your thread library, the "buildable blank
# P3" we give you can't build libthrgrp.a.  Once you install your thread
# libthrgrp.a:
410USER_LIBS_EARLY += libthrgrp.a

###########################################################################
# Object files for your syscall wrappers
###########################################################################

###########################################################################
# Object files for your automatic stack handling
###########################################################################

THREAD_OBJS = malloc.o asm.o mutex.o cond.o sem.o rwlock.o thread.o panic.o
# library and fix THREAD_OBJS above, uncomment this line to enable building
SYSCALL_OBJS = fork.o exec.o set_status.o vanish.o wait.o task_vanish.o   gettid.o yield.o deschedule.o make_runnable.o get_ticks.o sleep.o   new_pages.o remove_pages.o getchar.o readline.o print.o set_term_color.o   set_cursor_pos.o get_cursor_pos.o halt.o misbehave.o swexn.o readfile.o
AUTOSTACK_OBJS = autostack.o

# TODO: add libc0 objs here
C0_OBJS = cc0main.o args.o args_c0ffi.o conio.o conio_c0ffi.o coniovm.o file.o file_c0ffi.o file_util.o parse.o parse_c0ffi.o string.o string_c0ffi.o bare.o cstr.o

C0CONCUR_OBJS = channel.o queue.o util.o
