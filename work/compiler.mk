# -*- makefile -*-
# Select compiler by changing CC.

ifeq (default,$(origin CC))
    CC=gcc
endif
