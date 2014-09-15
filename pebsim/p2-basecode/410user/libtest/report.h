/** @file 410user/libtest/report.h
 *  @brief Test reporting interface
 *  @author elly1 U2009
 */

#ifndef LIBTEST_REPORT_H
#define LIBTEST_REPORT_H

extern const char* test_name;     /* Defined by the test in question with
                                   * DEF_TEST_NAME() */

enum {
    START_CMPLT = 0,
    START_ABORT = 1,
    START_4EVER = 2,
};

/** @brief Formats a test start message into a buffer.
 *  @param buf Buffer to format into.
 *  @param len Length of the buffer.
 *  @param type Type of test start.
 *  @return Void.
 */
extern void format_start(char *buf, int len, int type);

/** @brief Reports the start of a test.
 *  @param type Type of test start.
 */
extern void report_start(int type);

enum {
    END_SUCCESS = 0,
    END_FAIL = 1,
};

/** @brief Formats a test end message into a buffer.
 *  @param buf Buffer to format into.
 *  @param len Length of the buffer.
 *  @param type Type of test ending.
 *  @return Void.
 */
extern void format_end(char *buf, int len, int type);

/** @brief Reports the end of a test.
 *  @param type Type of test end.
 */
extern void report_end(int type);

/** @brief Formats a miscellaneous message into a buffer.
 *  @param buf Buffer to format into.
 *  @param len Length of the buffer.
 *  @param msg Message to format.
 *  @return Void.
 */
extern void format_misc(char *buf, int len, const char *msg);

/** @brief Report a miscellaneous happenstance.
 *  @param msg Happenstance.
 *  @return Void.
 */
extern void report_misc(const char *msg);

/** @brief Report an arbitrary formatted message.
 *  @param fmt Format.
 *  @param ... Varargs.
 *  @return Void.
 */
extern void report_fmt(const char *fmt, ...);

/** @brief Formats an error message into a buffer.
 *  @param buf Buffer to format into.
 *  @param len Length of the buffer.
 *  @param msg Message to format.
 *  @param code Error code.
 *  @return Void.
 */
extern void format_err(char *buf, int len, const char *msg, int code);

/** @brief Report an error.
 *  @param msg Error message.
 *  @param code Error code.
 *  @return Void.
 */
extern void report_err(const char *msg, int code);

/** @brief Report an error, and fail out.
 *  @param msg Error message.
 *  @param code Error code.
 *  @return Void.
 */
extern void report_fatal(const char *msg, int code);

/** @brief Require that an expression succeed.
 *  @param exp Expression, as a string.
 *  @param line Line number of the expression.
 *  @param v Expression value.
 *  @return Void.
 */
extern void report_on_err(const char *exp, int line, int v);

/** @brief Fail the test if an expression fails.
 *  @param exp Expression, as a string.
 *  @param line Line number of the expression.
 *  @param v Expression value.
 *  @return Void.
 */
extern void fatal_on_err(const char *exp, int line, int v);

#endif /* !LIBTEST_REPORT_H */
