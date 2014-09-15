410K_BOOT_OBJS := \
				util_lmm.o     \
				util_cmdline.o \

410K_BOOT_OBJS := $(410K_BOOT_OBJS:%=$(410KDIR)/boot/%)

ALL_410KOBJS += $(410K_BOOT_OBJS)
410KCLEANS += $(410KDIR)/libboot.a

$(410KDIR)/libboot.a: $(410K_BOOT_OBJS)

KERNEL_BOOT_HEAD += $(410KDIR)/boot/head.o
410KCLEANS += $(KERNEL_BOOT_HEAD)
