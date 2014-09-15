410ULIB_STRING_OBJS:= \
                        memcmp.o     \
                        memset.o     \
                        rindex.o     \
                        strcat.o     \
                        strchr.o     \
                        strcmp.o     \
                        strcpy.o     \
                        strcspn.o    \
                        strdup.o     \
                        strlen.o     \
                        strncat.o    \
                        strncmp.o    \
                        strncpy.o    \
                        strpbrk.o    \
                        strrchr.o    \
                        strspn.o     \
                        strstr.o     \
                        strtok.o     \


410ULIB_STRING_OBJS:= $(410ULIB_STRING_OBJS:%=$(410UDIR)/libstring/%)

ALL_410UOBJS += $(410ULIB_STRING_OBJS)
410UCLEANS += $(410UDIR)/libstring.a

$(410UDIR)/libstring.a: $(410ULIB_STRING_OBJS)
