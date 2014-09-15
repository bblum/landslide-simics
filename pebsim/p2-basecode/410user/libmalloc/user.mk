410U_MALLOC_OBJS := malloc.o memlib.o mm_malloc.o
410U_MALLOC_OBJS := $(410U_MALLOC_OBJS:%=$(410UDIR)/libmalloc/%)

ALL_410UOBJS += $(410U_MALLOC_OBJS)
410UCLEANS += $(410UDIR)/libmalloc.a

$(410UDIR)/libmalloc.a: $(410U_MALLOC_OBJS)
