/** @file 410user/libtest/test.h
 *  @brief Test library.
 *  @author elly1 U2009
 */

#ifndef LIBTEST_TEST_H
#define LIBTEST_TEST_H

/** @brief Jump to 'func' with the system under low-memory conditions.
 *  The reason why this takes a function pointer may not be immediately clear.
 *  Once exhaustion() returns to C, the compiler is immediately free to save off
 *  all the registers and generally touch the stack; this could cause a failed
 *  stack page allocation and lead to the thread being killed, which would fail
 *  the test. As such, exhaustion guarantees that once you are out of memory, it
 *  will not touch the stack on your behalf.
 *
 *  @param func Function to jump to.
 *  @param arg Argument passed to func in %ebx.
 *  @return Does not return.
 */
extern void exhaustion(void (*func)(void *), void *arg) __attribute__((noreturn));

/** @brief Fork, and exit immediately, printing success regardless of what
 *         happened. Does not touch the stack at all.
 *  @param arg Argument passed in *in %ebx*.
 *  @return Does not return.
 *
 *  NOTE:
 *  Do not call this function directly. It is designed to be passed into
 *  exhaustion().
 */
extern void fork_and_exit(void *arg) __attribute__((noreturn));

/** @brief Exit immediately, with a success message.
 *  @param arg Argument passed in *in %ebx*.
 *  @return Does not return.
 *
 *  See the note attached to fork_and_exit().
 */
extern void exit_success(void *arg) __attribute__((noreturn));

/** @brief Execute an illegal instruction.
 *  @return Void.
 */
extern void illegal(void);

/** @brief Assuredly invoke the misbehave() system call
 *  @return Void ("you are not expected to understand this")
 */
extern void assuredly_misbehave(int mode);

#endif /* !LIBTEST_TEST_H */
