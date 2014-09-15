410U_X86_OBJS := \
				bcopy.o   \
				bzero.o   \
				gccisms.o   \

410U_X86_OBJS := $(410U_X86_OBJS:%=$(410UDIR)/libx86/%)

ALL_410UOBJS += $(410U_X86_OBJS)
410UCLEANS += $(410UDIR)/libx86.a

$(410UDIR)/libx86.a: $(410U_X86_OBJS)
