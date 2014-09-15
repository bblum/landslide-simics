#include <stdio.h>
#include <thread.h>
#include <syscall.h> /* for PAGE_SIZE */
#include <assert.h>

void *
waiter(void *p)
{
  thr_exit((void *)'!');

  while(1)
	continue; /* placate compiler portably */
}

int main()
{
  thr_init(16 * PAGE_SIZE);
  int status;
  
  
  int child = thr_create(waiter, NULL);
  thr_join(child, (void **)&status);
  assert(status == '!');
  thr_exit(NULL);
  while(1)
	continue; /* placate compiler portably */
}
