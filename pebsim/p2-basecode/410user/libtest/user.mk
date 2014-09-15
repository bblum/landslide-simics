410U_TEST_OBJS := test.o testasm.o report.o
410U_TEST_OBJS := $(410U_TEST_OBJS:%=$(410UDIR)/libtest/%)
ALL_410UOBJS += $(410U_TEST_OBJS)
410UCLEANS += $(410UDIR)/libtest.a

$(410UDIR)/libtest.a: $(410U_TEST_OBJS)
