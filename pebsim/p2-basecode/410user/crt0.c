/* @file crt0.c
 * @brief The 15-410 userland C runtime setup function
 *
 * @note The build infrastructure "knows" that _main is the entrypoint
 *       function and will do the right thing as a result.
 */

#include <stdlib.h>
#include <syscall.h>
#include <assert.h>

extern int main(int argc, char *argv[]);
extern void install_autostack(void * stack_high, void * stack_low);

void _main(int argc, char *argv[], void *stack_high, void *stack_low)
{
  install_autostack(stack_high, stack_low);
  exit(main(argc, argv));
}
