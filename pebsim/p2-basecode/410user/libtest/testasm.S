/** @file 410user/libtest/testasm.S
 *  @brief Test library.
 *  @author elly1 U2009
 */

#include <simics.h>
#include <syscall_int.h>

.global exhaustion

#define EX_BASE     0x20000000
#define EX_SIZE     1024 * 1024 * 1024

/* %edi - continuation
 * %esi - syscall arg frame
 * %ebx - argument
 */
exhaustion:
    mov 4(%esp), %edi
    mov 8(%esp), %ebx
    lea -8(%esp), %esi  /* argument frame space */
    mov $EX_BASE, %ecx
    mov $EX_SIZE, %edx
    mov $4, %esp        /* don't tempt the user to think that their stack is
                         * still live */
loop:
    cmp $4096, %edx
    jb 1f
    mov %ecx, (%esi)
    mov %edx, 4(%esi)
    int $NEW_PAGES_INT
    test %eax, %eax
    jnz smaller
    add %edx, %ecx
    jmp loop
smaller:
    shr $1, %edx
    jmp loop
1:
    jmp *%edi

.global fork_and_exit

fork_and_exit:
    mov %ebx, %ecx
    int $FORK_INT
    test %eax, %eax
    jz 1f
    INLINE_SIMCALL(SIM_PUTS, %ecx, $0)
1:
    mov $0, %esi
    int $SET_STATUS_INT
    int $VANISH_INT

.global exit_success

exit_success:
    mov %ebx, %ecx
    INLINE_SIMCALL(SIM_PUTS, %ecx, $0)
    INLINE_SIMCALL(SIM_TEST_REPORT, %ecx, $1)
    mov $0, %esi
    int $SET_STATUS_INT
    int $VANISH_INT

.global illegal

illegal:
    xor %eax, %eax
    mov %eax, %cr0
    ret

/* Don't code like this:  "authorized personnel only". */
.global assuredly_misbehave
assuredly_misbehave:
    pushl %ebp
    movl %esp, %ebp
    pusha
    movl 8(%ebp), %esi
    int $MISBEHAVE_INT
    popa
    leave
    ret
