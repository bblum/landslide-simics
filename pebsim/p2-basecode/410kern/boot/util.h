#ifndef _410KERN_MULTIBOOT_UTILS_H_
#define _410KERN_MULTIBOOT_UTILS_H_

#include <boot/multiboot.h>
#include <lmm/lmm.h>

void mb_util_cmdline(mbinfo_t *, int *, char ***, char ***);
void mb_util_lmm(mbinfo_t *, lmm_t *);

#endif
