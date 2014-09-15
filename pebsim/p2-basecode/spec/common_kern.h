#ifndef _SPEC_COMMON_KERN_H_
#define _SPEC_COMMON_KERN_H_

/*********************************************************************/
/*                                                                   */
/* Interface between Multiboot and our kernel                        */
/*                                                                   */
/*********************************************************************/

/** @brief Return total number of frames (each of PAGE_SIZE bytes)
  *        that our machine is currently configured with.
  *
  * @return Frame count.
  */
int machine_phys_frames(void);

#include <boot/multiboot.h>

    /** @brief User kernel entrypoint
     *
     * @param mbinfo The kernel's multiboot information structure.
     * @param argc   Argument count, as parsed from multiboot.
     * @param argv   Argument vector, as parsed from multiboot.
     * @param envp   Environment, as parsed from multiboot.
     *
     * @pre malloc_lmm will have been initialized, stripped of regions that
     *      are potentially hazardous, and bounded between 1 and 16M.
     *
     * @note If you plan to write your own allocator for space, you should
     *       still treat LMM as the official mechanism of getting physical
     *       frames in the kernel region, unless you are planning to parse
     *       the multiboot information and/or e820 yourself.
     */
int kernel_main(mbinfo_t *mbinfo, int argc, char **argv, char **envp);

/*********************************************************************/
/*                                                                   */
/* Kernel memory layout requirements                                 */
/*                                                                   */
/*********************************************************************/

#define USER_MEM_START 0x01000000

#endif /* _SPEC_COMMON_KERN_H_ */
