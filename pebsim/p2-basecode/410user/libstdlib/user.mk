410ULIB_STDLIB_OBJS := \
						abs.o     \
						atol.o    \
						ctype.o   \
						exit.o   \
                        qsort.o   \
                        rand.o    \
						strtol.o  \
						strtoul.o \


410ULIB_STDLIB_OBJS := $(410ULIB_STDLIB_OBJS:%=$(410UDIR)/libstdlib/%)

ALL_410UOBJS += $(410ULIB_STDLIB_OBJS)
410UCLEANS += $(410UDIR)/libstdlib.a

$(410UDIR)/libstdlib.a: $(410ULIB_STDLIB_OBJS)
