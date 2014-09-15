/* Test program for 15-410 project 3 Fall 2003
 * Tadashi Okoshi (slash)
 */

#ifndef _410_TESTS_H_
#define _410_TESTS_H_

#define TEST_START_CMPLT " START__TYPE_COMPLETE"
#define TEST_START_ABORT " START__TYPE_ABORT"
#define TEST_START_4EVER " START__TYPE_FOREVER"
  /** An obsoleted progress detection mechanism. */
#define TEST_START_4EVER_PROGRESS " START__TYPE_FOREVER_PROGRESS"

#define TEST_END_SUCCESS " END__SUCCESS"
#define TEST_END_FAIL    " END__FAIL"

/*Don't change this for now. (slash)*/
#define TEST_PFX "(^_^)_"

#include <simics.h>    /* lprintf() */

#ifndef ASSEMBLER

#include <report.h>    /* report_start(), etc. */

/* macros added by mtomczak */
/* macro assumptions: 
   1) static char test_name[] is defined (using DEF_TEST_NAME macro)
   2) In each function, REPORT_LOCAL_INIT called before REPORT_ON_ERR
*/

#define DEF_TEST_NAME(x) const char *test_name = x

#define REPORT_START_CMPLT  report_start(START_CMPLT)
#define REPORT_START_ABORT  report_start(START_ABORT)
#define REPORT_START_4EVER  report_start(START_4EVER)

#define REPORT_END_SUCCESS  report_end(END_SUCCESS)
#define REPORT_END_FAIL     report_end(END_FAIL)

#define REPORT_LOCAL_INIT
#define REPORT_MISC(x)              report_misc(x)
#define REPORT_ERR(x,code)          report_err(x, code)
#define REPORT_FAIL_ERR(x,code)     report_fatal(x, code)
#define REPORT_ON_ERR(exp)          report_on_err(#exp, __LINE__, exp)
#define REPORT_FAILOUT_ON_ERR(exp)  fatal_on_err(#exp, __LINE__, exp)

/**************************************************/
/*memo*/
/*

//At the beginning of the test code.
lprintf("%s%s%s",TEST_PFX,test_name,TEST_START_CMPLT); 
 or
lprintf("%s%s%s",TEST_PFX,test_name,TEST_START_ABORT);
 or
lprintf("%s%s%s",TEST_PFX,test_name,TEST_START_4EVER);

//In the test body...
....
lprintf("%s%s%s",TEST_PFX,test_name,"foo mesg.");
lprintf("%s%s%s",TEST_PFX,test_name,"bar mesg.");
....

//At the end of the test (only START_CMPLT case)
lprintf("%s%s%s",TEST_PFX,test_name,TEST_END_SUCCESS);
 or
lprintf("%s%s%s",TEST_PFX,test_name,TEST_END_FAIL);

*/

#define TEST_PROG_ENGAGE(i) sim_fr_prog(i)
#define TEST_PROG_PROGRESS  sim_fr_here()

#endif /* !ASSEMBLER */

#endif /* _410_TESTS_H_ */
