410U_SIMICS_OBJS := \
				simics.o   \
				simics_c.o 

410U_SIMICS_OBJS := $(410U_SIMICS_OBJS:%=$(410UDIR)/libsimics/%)

ALL_410UOBJS += $(410U_SIMICS_OBJS)
410UCLEANS += $(410UDIR)/libsimics.a

$(410UDIR)/libsimics.a: $(410U_SIMICS_OBJS)
