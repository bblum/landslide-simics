410ULIB_STDIO_OBJS := \
						doprnt.o  \
						doscan.o  \
						hexdump.o \
						printf.o  \
						putchar.o \
						puts.o    \
						sprintf.o \
						sscanf.o  \

410ULIB_STDIO_OBJS := $(410ULIB_STDIO_OBJS:%=$(410UDIR)/libstdio/%)

ALL_410UOBJS += $(410ULIB_STDIO_OBJS)
410UCLEANS += $(410UDIR)/libstdio.a

$(410UDIR)/libstdio.a: $(410ULIB_STDIO_OBJS)
