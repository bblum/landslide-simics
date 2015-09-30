/** @file 410user/libtest/report.c
 *  @brief Test reporting implementation
 *  @author elly1 U2009
 */

#include <stdio.h>          /* snprintf */
#include <stdlib.h>         /* exit */
#include <report.h>

#include "410_tests.h"      /* TEST_START_*, TEST_END_*, TEST_PFX */

/* This is a GNUism used for the sake of maintainability. This way nothing
 * horrible will happen if someone reorders the START_* constants in the header
 * file. */
const char *start_msgs[] = {
    [START_CMPLT] = TEST_START_CMPLT,
    [START_ABORT] = TEST_START_ABORT,
    [START_4EVER] = TEST_START_4EVER,
};

const char *end_msgs[] = {
    [END_SUCCESS] = TEST_END_SUCCESS,
    [END_FAIL]    = TEST_END_FAIL,
};

void format_start(char *buf, int len, int type) {
    snprintf(buf, len, "%s%s%s", TEST_PFX, test_name, start_msgs[type]);
}

void format_end(char *buf, int len, int type) {
    snprintf(buf, len, "%s%s%s", TEST_PFX, test_name, end_msgs[type]);
}

void format_misc(char *buf, int len, const char *msg) {
    snprintf(buf, len, "%s%s%s", TEST_PFX, test_name, msg);
}

void format_err(char *buf, int len, const char *msg, int code) {
    snprintf(buf, len, "%s%s%s%d", TEST_PFX, test_name, msg, code);
}

void report_start(int type) {
    char buf[256];
    format_start(buf, sizeof(buf), type);
    sim_puts(buf);
}

void report_end(int type) {
    char buf[256];
    format_end(buf, sizeof(buf), type);
    sim_puts(buf);

    //hahaha don't do this in landslide.
    //int success = (type == END_SUCCESS) ? 1 : 0;
    //sim_update_scoreboard((char *)test_name, success);
}

void report_misc(const char *msg) {
    char buf[256];
    format_misc(buf, sizeof(buf), msg);
    sim_puts(buf);
}

void report_fmt(const char *fmt, ...) {
    va_list ap;
    char b[256];
    char buf[256];
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    snprintf(buf, sizeof(buf), "%s%s%s", TEST_PFX, test_name, b);
    sim_puts(buf);
}

void report_err(const char *msg, int code) {
    char buf[256];
    format_err(buf, sizeof(buf), msg, code);
    sim_puts(buf);
}

void report_fatal(const char *msg, int code) {
    report_err(msg, code);
    report_end(END_FAIL);
}

void report_on_err(const char *exp, int line, int v) {
    char buf[256];
    if (v >= 0) { return; }
    snprintf(buf, sizeof(buf), "%s%sErr %d on line %d: `%s'",
             TEST_PFX, test_name, v, line, exp);
    sim_puts(buf);
}

void fatal_on_err(const char *exp, int line, int v) {
    report_on_err(exp, line, v);
    if (v >= 0) { return; }
    report_end(END_FAIL);
    panic("test program crashed: %s", exp);
    exit(v);
}
