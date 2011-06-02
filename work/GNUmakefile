# -*- makefile -*-
# Do not edit. This file will be overwritten by the workspace setup script.

ifneq ($(VERBOSE),yes)
MAKEFLAGS += -s
endif

include config.mk

ifeq ($(TARGET_DIR),)
TARGET_DIR=$(SIMICS_WORKSPACE)/$(HOST_TYPE)/lib
endif

MODULES=$(patsubst modules/%/Makefile,%,$(wildcard modules/*/Makefile))

export TARGET_DIR

all: $(MODULES)
.PHONY: $(MODULES)

ifeq ($(ENVCHECK),disable)
ENVCHECK_FLAG=
else
include $(SIMICS_MODEL_BUILDER)/config/workspace/envcheck.mk
ENVCHECK_FLAG=$(HOST_TYPE)/.environment-check/all
endif

MODULE_CLEAN   := $(foreach MOD,$(MODULES),clean-$(MOD))
MODULE_CLOBBER := $(foreach MOD,$(MODULES),clobber-$(MOD))

$(MODULES): $(ENVCHECK_FLAG)
ifeq (,$(HOST_TYPE))
	echo "Could not determine host-type."
	exit 1
endif
	[ -d "$(TARGET_DIR)" ] || mkdir -p "$(TARGET_DIR)"
	[ -d "$(SIMICS_WORKSPACE)/$(HOST_TYPE)/obj/modules/$@" ] || \
		mkdir -p "$(SIMICS_WORKSPACE)/$(HOST_TYPE)/obj/modules/$@"
	@echo "$(HEAD_COLOR)=== Building module \"$@\" ===$(NO_COLOR)"
	$(MAKE) --directory="$(SIMICS_WORKSPACE)/$(HOST_TYPE)/obj/modules/$@" \
		-f "$(SIMICS_WORKSPACE)/modules/$@/Makefile" \
		MOD_MAKEFILE="$(SIMICS_WORKSPACE)/modules/$@/Makefile" \
		HOST_TYPE=$(HOST_TYPE) \
                SRC_BASE="$(SIMICS_WORKSPACE)/modules" \
		MODULE_MAKEFILE="$(SIMICS_MODEL_BUILDER)/config/workspace/module.mk" \
		TARGET=$@

CLEAN_TARGET=$(patsubst clean-%,%,$@)
CLOBBER_TARGET=$(patsubst clobber-%,%,$@)
MODULE_PYNAME= $(subst -,_,$(CLOBBER_TARGET))

$(MODULE_CLEAN):
	echo "Clean of module: $(CLEAN_TARGET)" ; 	 	\
        $(RM) -rf $(HOST_TYPE)/obj/modules/$(CLEAN_TARGET)

$(MODULE_CLOBBER):
	echo "Removing module: $(CLOBBER_TARGET)" ;		\
        $(RM) -rf $(HOST_TYPE)/obj/modules/$(CLOBBER_TARGET) ;	\
	$(RM) -f $(HOST_TYPE)/lib/$(CLOBBER_TARGET).$(SO_SFX) ;	\
	$(RM) -f $(HOST_TYPE)/lib/python/mod_$(MODULE_PYNAME)*; \
	$(RM) -f $(HOST_TYPE)/lib/python/$(MODULE_PYNAME).py*

clean:
	echo "Clean of all modules"
	$(RM) -rf $(HOST_TYPE)/obj/*
	$(RM) -rf $(HOST_TYPE)/.environment-check

clobber: clean
	echo "Removing all modules"
	$(RM) -rf $(HOST_TYPE)/lib/*

help:
	@echo Available modules: $(MODULES)
	@echo Show build commands: make VERBOSE=yes

test: all
	$(SIMICS_WORKSPACE)/bin/test-runner

test-%: %
	$(SIMICS_WORKSPACE)/bin/test-runner modules/$*
