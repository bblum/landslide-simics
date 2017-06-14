# -*- makefile -*-
# Do not edit. This file will be overwritten by the workspace setup script.

.SUFFIXES:

ifneq ($(VERBOSE),yes)
    MAKEFLAGS += -s
endif

include config.mk

ifeq (,$(HOST_TYPE))
    $(error "Could not determine host-type (HOST_TYPE variable not set)")
endif

ifeq ($(TARGET_DIR),)
    TARGET_DIR=$(SIMICS_WORKSPACE)/$(HOST_TYPE)/lib
endif
export TARGET_DIR

.PHONY: default
default: all

ifeq ($(ENVCHECK),disable)
    ENVCHECK_FLAG=
else
    include $(SIMICS_MODEL_BUILDER)/config/workspace/envcheck.mk
    ENVCHECK_FLAG=$(HOST_TYPE)/.environment-check/all
endif

MODULE_MAKEFILE := $(SIMICS_MODEL_BUILDER)/config/workspace/module.mk

ifeq (,$(WORKSPACE_SRC_DIR))
WORKSPACE_SRC_DIR=modules
endif

ifeq ($(DEEP_SOURCE_SEARCH),yes)

# build targets.cache as a side effect of a $(shell) call. This is ugly, but
# less ugly than the alternatives
_:=$(shell								\
         mkdir -p $(TARGET_DIR)						\
	 && $(PYTHON)							\
	    $(SIMICS_MODEL_BUILDER)/scripts/build/build-target-cache.py	\
	    --source-dir $(WORKSPACE_SRC_DIR) > $(TARGET_DIR)/targets.cache)

-include $(TARGET_DIR)/targets.cache

else

# find the modules the old fashion way
MODULES=$(patsubst $(WORKSPACE_SRC_DIR)/%/Makefile,%,$(wildcard $(WORKSPACE_SRC_DIR)/*/Makefile))

.PHONY: all test help $(MODULES)
all: $(MODULES)

help:
	@echo Available modules: $(MODULES)
	@echo Show build commands: make VERBOSE=yes

test: all
	$(SIMICS_WORKSPACE)/bin/test-runner

test-%: %
	$(SIMICS_WORKSPACE)/bin/test-runner $(WORKSPACE_SRC_DIR)/$*

$(MODULES): $(ENVCHECK_FLAG)
	mkdir -p "$(TARGET_DIR)"
	mkdir -p "$(SIMICS_WORKSPACE)/$(HOST_TYPE)/obj/modules/$@"
	@echo "=== Building module \"$@\" ==="
	$(MAKE) -r -R								\
	    --directory="$(SIMICS_WORKSPACE)/$(HOST_TYPE)/obj/modules/$@"	\
	    -f "$(SIMICS_WORKSPACE)/$(WORKSPACE_SRC_DIR)/$@/Makefile"		\
	    MOD_MAKEFILE="$(SIMICS_WORKSPACE)/$(WORKSPACE_SRC_DIR)/$@/Makefile"	\
	    HOST_TYPE=$(HOST_TYPE)						\
            SRC_BASE="$(SIMICS_WORKSPACE)/$(WORKSPACE_SRC_DIR)"			\
	    MODULE_MAKEFILE="$(MODULE_MAKEFILE)"				\
	    TARGET=$@

endif

MODULE_CLEAN   := $(addprefix clean-,$(MODULES))
MODULE_CLOBBER := $(addprefix clobber-,$(MODULES))

CLEAN_TARGET=$(patsubst clean-%,%,$@)
CLOBBER_TARGET=$(patsubst clobber-%,%,$@)
MODULE_PYNAME= $(subst -,_,$(CLOBBER_TARGET))

.PHONY: $(MODULE_CLEAN) $(MODULE_CLOBBER) clean clobber

$(MODULE_CLEAN):
	echo "Clean of module: $(CLEAN_TARGET)" ; 	 	\
        rm -rf $(HOST_TYPE)/obj/modules/$(CLEAN_TARGET)

$(MODULE_CLOBBER):
	echo "Removing module: $(CLOBBER_TARGET)" ;		\
	rm -rf $(HOST_TYPE)/obj/modules/$(CLOBBER_TARGET) ;	\
	rm -f $(HOST_TYPE)/lib/$(CLOBBER_TARGET).$(SO_SFX) ;	\
	rm -f $(HOST_TYPE)/lib/python/mod_$(MODULE_PYNAME)*; \
	rm -f $(HOST_TYPE)/lib/$(MODULE_PYNAME).py*

clean:
	echo "Clean of all modules"
	rm -rf $(HOST_TYPE)/obj/*
	rm -rf $(HOST_TYPE)/.environment-check

clobber: clean
	echo "Removing all modules"
	rm -rf $(HOST_TYPE)/lib/*
