###################### LOCAL BUILD TARGETS ############################
#410KERNEL_OBJS = entry.o

410KOBJS = $(410KERNEL_OBJS:%=$(410KDIR)/%)
ALL_410KOBJS += $(410KOBJS)

410KLIBS = $(410KERNEL_LIBS:%=$(410KDIR)/%)
410KCLEANS += $(410KDIR)/partial_kernel.o

$(410KDIR)/partial_kernel.o : $(410KOBJS)
	$(LD) -r $(LDFLAGS) -o $@ $^

########################### LIBRARY INCLUSIONS ################################
ifneq (,$(410KERNEL_LIBS))
include $(patsubst lib%.a,$(410KDIR)/%/kernel.mk,$(410KERNEL_LIBS))
endif
