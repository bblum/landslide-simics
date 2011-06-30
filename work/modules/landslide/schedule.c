/**
 * @file schedule.c
 * @brief Thread scheduling logic for landslide
 * @author Ben Blum
 */

#include <simics/api.h>

#include "landslide.h"

/* Assumptions we need to make about the kernel */
/* pobbles */
#define GUEST_CURRENT_TCB 0x00152420
#define GUEST_TCB_TID_OFFSET 0

/* Returns the tid of the currently scheduled thread. */
int get_current_thread(ls_state_t *ls)
{
	int tcb = SIM_read_phys_memory(ls->cpu0, GUEST_CURRENT_TCB, 4);
	return SIM_read_phys_memory(ls->cpu0, tcb + GUEST_TCB_TID_OFFSET, 4);
}
