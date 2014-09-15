/* simics.S - Simics interface (user side) */

/* sim_call(ebx, ecx, edx) */
.global sim_call
sim_call:
    push %ebx
    mov 0x8(%esp), %ebx
    mov 0xC(%esp), %ecx
    mov 0x10(%esp), %edx
    mov $0, %eax
    xchg %ebx, %ebx
    pop %ebx
    ret
