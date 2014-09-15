/**
 * @file boot/head.S
 * @author matthewj
 * @author de0u
 */

/* N.B. This is NOT to be construed as style guidance!! */
/* N.B. This is NOT to be construed as style guidance!! */
/* N.B. This is NOT to be construed as style guidance!! */

#include <simics.h>
#include <x86/video_defines.h>
#include <x86/seg.h>

/** @brief Entry point from the bootloader */
.global _start
/** @brief Start of the kernel image */
.global __kimg_start
/** @brief Start of the init GDT */
.global init_gdt
/** @brief Start of the init IDT */
.global init_idt
/** @brief Start of the init TSS */
.global init_tss

.text

__kimg_start:

mb_header:
    /* Not a complete Multiboot header, but GRUB can live with it. */
    .long 0x1BADB002      /**< Multiboot magic word */
    .long 0x00000000      /**< Flags field */
    .long (0 - 0x1BADB002 - 0x00000000)   /**< Checksum */
    .space 244

init_idt: /* 0x100100 */
    .space 64
    .long 0x00100998
    .long 0x00108e00
    .space 1976
init_gdt: /* 0x100900 */
    .long 0x00000000
    .long 0x00000000
    .long 0x09300067
    .long 0x00008910
    .long 0x0000ffff
    .long 0x00cf9b00
    .long 0x0000ffff
    .long 0x00cf9300
    .long 0x0000ffff
    .long 0x00cffb00
    .long 0x0000ffff
    .long 0x00cff200
init_tss: /* 0x100930 */
    .space 8
    .word 0x0018
    .space 92 /* fills with 0's */
    .word 0x0068
df_handler: /* 0x100998 */
    cli
    /* Get something "out there" first thing */
    leal fmsg, %ecx
    movl $SIM_PUTS, %ebx
    xchg %ebx, %ebx
    /* We got here because we were in trouble, so be conservative */
    movl $SEGSEL_KERNEL_DS, %eax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    leal istack, %esp
    subl $2048, %esp      /* try to avoid wiping state */
    /* Now we probably have a runtime environment */
    leal fbytes, %eax
    pushl %eax
    call blat
df_handler_loop:
    jmp df_handler_loop

_start:
    cli
    leal istack, %esp     /* Load initial stack pointer */

    movl $init_tss, %eax
    subl $init_gdt, %eax
    subl $1, %eax
    pushl %eax            /* gdt limit == sizeof(gdt)-1 */
    pushl $init_gdt
    call lgdt
    addl $8, %esp

    movl $init_gdt, %eax
    subl $init_idt, %eax
    subl $1, %eax
    pushl %eax            /* idt limit == sizeof(idt)-1 */
    pushl $init_idt
    call lidt
    addl $8, %esp

    movl $SEGSEL_TSS, %eax
    ltr %ax

    pushl %esp            /* Pass in the top of the initial stack */
    pushl %ebx            /* Pass in the multiboot structure */
    call mb_entry         /* Call the C entry point */

    pushl $pbytes
    call blat
stuck:
    jmp stuck

# void blat(uint8_t *bytes)
blat:
    pushl %ebp
    movl %esp, %ebp
    pushl %ebx
    pushl %esi
    pushl %edi

    movl 8(%ebp), %eax                 # bytes
    pushl %eax
    call strlen
    movl %eax, %edx                    # EDX: strlen(bytes)
    add $4, %esp

    movl $CONSOLE_MEM_BASE, %edi       # EDI: dest

    movl $CONSOLE_WIDTH, %ebx
    imull $CONSOLE_HEIGHT, %ebx
    sall $1, %ebx                      # 2*width*height
    addl %edi, %ebx                    # EBX: limit
    subl %edx, %ebx                    # EBX: limit (round down)

    movl %es, %eax
    pushl %eax                         # save ES
    movl %ds, %eax
    movl %eax, %es                     # force ES to match DS

movs:
    movl %edx, %ecx                    # ECX: count
    movl 8(%ebp), %esi                 # ESI: source
    rep movsb                          # ECX bytes from DS:ESI to ES:EDI
    cmpl %edi, %ebx
    jg movs

    popl %eax
    movl %eax, %es                     # restore ES

    popl %edi
    popl %esi
    popl %ebx
    popl %ebp
    ret

.data

fmsg:
    .asciz "\n***** Double fault!\n"
fbytes:
    .asciz "D\ro\ru\rb\rl\re\r \rf\ra\ru\rl\rt\r!\r \r"
    
pmsg:
    .asciz "Kernel cannot return()!"
pbytes:
    .asciz "K\re\rr\rn\re\rl\r \rc\ra\rn\rn\ro\rt\r \rr\re\rt\ru\rr\rn\r(\r)\r!\r \r"

/* Initial stack */
.space 4096
istack:
