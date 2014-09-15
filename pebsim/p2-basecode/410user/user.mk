################# TARGET-SPECIFIC RULES #################
$(410UDIR)/exec2obj.dep: INCLUDES=$(KINCLUDES)
$(410UDIR)/exec2obj.dep: CFLAGS=-m32 -Wall -Werror

# This wants to use the system standard library and everything. I'm not sure how this ever
# worked; probably only because of the use of -I-...
$(410UDIR)/exec2obj: $(410UDIR)/exec2obj.c
	$(CC) -m32 -I. -Wall -Werror -o $@ $^
#	$(CC) -m32 -iquote 410kern/inc -Wall -Werror -o $@ $^
#	$(CC) -m32 $(KINCLUDES) -Wall -Werror -o $@ $^

ifeq (1,$(DO_INCLUDE_DEPS))
-include $(410UDIR)/exec2obj.dep
endif

410UCLEANS+=$(410UDIR)/exec2obj $(410UDIR)/exec2obj.dep

$(PROGS:%=$(BUILDDIR)/%) :
$(BUILDDIR)/%.strip : $(BUILDDIR)/%
	strip -o $@ $<

.INTERMEDIATE: $(PROGS:%=$(BUILDDIR)/%.strip)

# Generate a directory listing file so the shell can implement ls
$(BUILDDIR)/__DIR_LISTING__: $(PROGS:%=$(BUILDDIR)/%.strip) \
	                         $(FILES:%=$(BUILDDIR)/%)
	(printf "%s\0" $(sort $(PROGS) $(FILES)); printf "\0") > $(BUILDDIR)/__DIR_LISTING__

$(BUILDDIR)/user_apps.o: $(410UDIR)/exec2obj $(BUILDDIR)/__DIR_LISTING__ \
                         $(PROGS:%=$(BUILDDIR)/%.strip) \
                         $(FILES:%=$(BUILDDIR)/%)
	( cd $(BUILDDIR); \
      $(PROJROOT)/$(410UDIR)/exec2obj __DIR_LISTING__ $(PROGS) $(FILES)) | \
	  $(AS) --32 -o $@

include $(410UDIR)/$(UPROGDIR)/progs.mk

include $(patsubst %.a,$(410UDIR)/%/user.mk,$(410USER_LIBS_EARLY) $(410USER_LIBS_LATE))

################# STUDENT LIBRARY RULES #################

STUU_THREAD_OBJS := $(THREAD_OBJS:%=$(STUUDIR)/libthread/%)
ALL_STUUOBJS += $(STUU_THREAD_OBJS)
STUUCLEANS += $(STUUDIR)/libthread.a
$(STUUDIR)/libthread.a: $(STUU_THREAD_OBJS)

STUU_SYSCALL_OBJS := $(SYSCALL_OBJS:%=$(STUUDIR)/libsyscall/%)
ALL_STUUOBJS += $(STUU_SYSCALL_OBJS)
STUUCLEANS += $(STUUDIR)/libsyscall.a
$(STUUDIR)/libsyscall.a: $(STUU_SYSCALL_OBJS)

STUU_AUTOSTACK_OBJS := $(AUTOSTACK_OBJS:%=$(STUUDIR)/libautostack/%)
ALL_STUUOBJS += $(STUU_AUTOSTACK_OBJS)
STUUCLEANS += $(STUUDIR)/libautostack.a
$(STUUDIR)/libautostack.a: $(STUU_AUTOSTACK_OBJS)
