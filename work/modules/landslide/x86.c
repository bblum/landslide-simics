/**
 * @file x86.c
 * @brief x86-specific utilities
 * @author Ben Blum
 */

#include <simics/api.h>

#define MODULE_NAME "X86"
#define MODULE_COLOUR COLOUR_DARK COLOUR_GREEN

#include "common.h"
#include "kernel_specifics.h"
#include "landslide.h"
#include "student_specifics.h"
#include "x86.h"

/* Horribly, simics's attributes for the segsels are lists instead of ints. */
#define GET_SEGSEL(cpu, name) \
	SIM_attr_integer(SIM_attr_list_item(SIM_get_attribute(cpu, #name), 0))

/* two possible methods for causing a timer interrupt - the "immediately"
 * version makes the simulation immediately jump to some assembly on the stack
 * that directly invokes the timer interrupt INSTEAD of executing the pending
 * instruction; the other way just manipulates the cpu's interrupt pending
 * flags to make it do the interrupt itself. */
unsigned int cause_timer_interrupt_immediately(conf_object_t *cpu)
{
	unsigned int esp = GET_CPU_ATTR(cpu, esp);
	unsigned int eip = GET_CPU_ATTR(cpu, eip);
	unsigned int eflags = GET_CPU_ATTR(cpu, eflags);
	unsigned int handler = kern_get_timer_wrap_begin();

	if (KERNEL_MEMORY(eip)) {
		/* Easy mode. Just make a small iret stack frame. */
		assert(GET_SEGSEL(cpu, cs) == SEGSEL_KERNEL_CS);
		assert(GET_SEGSEL(cpu, ss) == SEGSEL_KERNEL_DS);

		lsprintf(DEV, "tock! (0x%x)\n", eip);

		/* 12 is the size of an IRET frame only when already in kernel mode. */
		unsigned int new_esp = esp - 12;
		SET_CPU_ATTR(cpu, esp, new_esp);
		SIM_write_phys_memory(cpu, new_esp + 8, eflags, 4);
		SIM_write_phys_memory(cpu, new_esp + 4, SEGSEL_KERNEL_CS, 4);
		SIM_write_phys_memory(cpu, new_esp + 0, eip, 4);
	} else {
		/* Hard mode - do a mode switch also. Grab esp0, make a large
		 * iret frame, and change the segsel registers to kernel mode. */
		assert(GET_SEGSEL(cpu, cs) == SEGSEL_USER_CS);
		assert(GET_SEGSEL(cpu, ss) == SEGSEL_USER_DS);

		lsprintf(DEV, "tock! from userspace! (0x%x)\n", eip);

		unsigned int esp0 = READ_MEMORY(cpu, (unsigned)GUEST_ESP0_ADDR);
		/* 20 is the size of an IRET frame coming from userland. */
		unsigned int new_esp = esp0 - 20;
		SET_CPU_ATTR(cpu, esp, new_esp);
		SIM_write_phys_memory(cpu, new_esp + 16, SEGSEL_USER_DS, 4);
		SIM_write_phys_memory(cpu, new_esp + 12, esp, 4);
		SIM_write_phys_memory(cpu, new_esp +  8, eflags, 4);
		SIM_write_phys_memory(cpu, new_esp +  4, SEGSEL_USER_CS, 4);
		SIM_write_phys_memory(cpu, new_esp +  0, eip, 4);

		/* Change %cs and %ss. (Other segsels should be saved/restored
		 * in the kernel's handler wrappers.) */
		attr_value_t cs = SIM_make_attr_list(10,
			SIM_make_attr_integer(SEGSEL_KERNEL_CS),
			SIM_make_attr_integer(1),
			SIM_make_attr_integer(0),
			SIM_make_attr_integer(1),
			SIM_make_attr_integer(1),
			SIM_make_attr_integer(1),
			SIM_make_attr_integer(11),
			SIM_make_attr_integer(0),
			SIM_make_attr_integer(4294967295L),
			SIM_make_attr_integer(1));
		set_error_t ret = SIM_set_attribute(cpu, "cs", &cs);
		assert(ret == Sim_Set_Ok && "failed set cs");
		SIM_free_attribute(cs);
		attr_value_t ss = SIM_make_attr_list(10,
			SIM_make_attr_integer(SEGSEL_KERNEL_DS),
			SIM_make_attr_integer(1),
			SIM_make_attr_integer(3),
			SIM_make_attr_integer(1),
			SIM_make_attr_integer(1),
			SIM_make_attr_integer(1),
			SIM_make_attr_integer(3),
			SIM_make_attr_integer(0),
			SIM_make_attr_integer(4294967295L),
			SIM_make_attr_integer(1));
		ret = SIM_set_attribute(cpu, "ss", &ss);
		assert(ret == Sim_Set_Ok && "failed set ss");
		SIM_free_attribute(ss);

		/* Change CPL. */
		assert(GET_CPU_ATTR(cpu, cpl) == 3);
		SET_CPU_ATTR(cpu, cpl, 0);

	}
	SET_CPU_ATTR(cpu, eip, handler);
	return handler;
}

/* i.e., with stallin' */
static void cause_timer_interrupt_soviet_style(conf_object_t *cpu, lang_void *x)
{
	SIM_stall_cycle(cpu, 0);
}

void cause_timer_interrupt(conf_object_t *cpu)
{
	lsprintf(DEV, "tick! (0x%x)\n", GET_CPU_ATTR(cpu, eip));

	if (GET_CPU_ATTR(cpu, pending_vector_valid)) {
		SET_CPU_ATTR(cpu, pending_vector,
			     GET_CPU_ATTR(cpu, pending_vector)
			     | TIMER_INTERRUPT_NUMBER);
	} else {
		SET_CPU_ATTR(cpu, pending_vector, TIMER_INTERRUPT_NUMBER);
		SET_CPU_ATTR(cpu, pending_vector_valid, 1);
	}

	SET_CPU_ATTR(cpu, pending_interrupt, 1);
	/* Causes simics to flush whatever pipeline, implicit or not, would
	 * otherwise let more instructions get executed before the interrupt be
	 * taken. */
	SIM_run_unrestricted(cpu, cause_timer_interrupt_soviet_style, NULL);
}

/* Will use 8 bytes of stack when it runs. */
#define CUSTOM_ASSEMBLY_CODES_STACK 8
static const char custom_assembly_codes[] = {
	0x50, /* push %eax */
	0x52, /* push %edx */
	0x66, 0xba, 0x20, 0x00, /* mov $0x20, %dx # INT_ACK_CURRENT */
	0xb0, 0x20, /* mov $0x20, %al # INT_CTL_PORT */
	0xee, /* out %al, (%dx) */
	0x5a, /* pop %edx */
	0x58, /* pop %eax */
	0xcf, /* iret */
};

unsigned int avoid_timer_interrupt_immediately(conf_object_t *cpu)
{
	unsigned int buf = GET_CPU_ATTR(cpu, esp) -
		(ARRAY_SIZE(custom_assembly_codes) + CUSTOM_ASSEMBLY_CODES_STACK);

	lsprintf(INFO, "Cuckoo!\n");

	STATIC_ASSERT(ARRAY_SIZE(custom_assembly_codes) % 4 == 0);
	for (int i = 0; i < ARRAY_SIZE(custom_assembly_codes); i++) {
		SIM_write_phys_memory(cpu, buf+i, custom_assembly_codes[i], 1);
	}

	SET_CPU_ATTR(cpu, eip, buf);
	return buf;
}

/* keycodes for the keyboard buffer */
static int i8042_key(char c)
{
	static const int i8042_keys[] = {
		['0'] = 18, ['1'] = 19, ['2'] = 20, ['3'] = 21, ['4'] = 22,
		['5'] = 23, ['6'] = 24, ['7'] = 25, ['8'] = 26, ['9'] = 27,
		['a'] = 28, ['b'] = 29, ['c'] = 30, ['d'] = 31, ['e'] = 32,
		['f'] = 33, ['g'] = 34, ['h'] = 35, ['i'] = 36, ['j'] = 37,
		['k'] = 38, ['l'] = 39, ['m'] = 40, ['n'] = 41, ['o'] = 42,
		['p'] = 43, ['q'] = 44, ['r'] = 45, ['s'] = 46, ['t'] = 47,
		['u'] = 48, ['v'] = 49, ['w'] = 50, ['x'] = 51, ['y'] = 52,
		['z'] = 53, ['\''] = 54, [','] = 55, ['.'] = 56, [';'] = 57,
		['='] = 58, ['/'] = 59, ['\\'] = 60, [' '] = 61, ['['] = 62,
		[']'] = 63, ['-'] = 64, ['`'] = 65,             ['\n'] = 67,
	};
	assert(i8042_keys[(int)c] != 0 && "Attempt to type an unsupported key");
	return i8042_keys[(int)c];
}

static bool i8042_shift_key(char *c)
{
	static const char i8042_shift_keys[] = {
		['~'] = '`', ['!'] = '1', ['@'] = '2', ['#'] = '3', ['$'] = '4',
		['%'] = '5', ['^'] = '6', ['&'] = '7', ['*'] = '8', ['('] = '9',
		[')'] = '0', ['_'] = '-', ['+'] = '=', ['Q'] = 'q', ['W'] = 'w',
		['E'] = 'e', ['R'] = 'r', ['T'] = 't', ['Y'] = 'y', ['U'] = 'u',
		['I'] = 'i', ['O'] = 'o', ['P'] = 'p', ['{'] = '[', ['}'] = ']',
		['A'] = 'a', ['S'] = 's', ['D'] = 'd', ['D'] = 'd', ['F'] = 'f',
		['G'] = 'g', ['H'] = 'h', ['J'] = 'j', ['K'] = 'k', ['L'] = 'l',
		[':'] = ';', ['"'] = '\'', ['Z'] = 'z', ['X'] = 'x',
		['C'] = 'c', ['V'] = 'v', ['B'] = 'b', ['N'] = 'n', ['M'] = 'm',
		['<'] = ',', ['>'] = '.', ['?'] = '/',
	};

	if (i8042_shift_keys[(int)*c] != 0) {
		*c = i8042_shift_keys[(int)*c];
		return true;
	}
	return false;
}

void cause_keypress(conf_object_t *kbd, char key)
{
	bool do_shift = i8042_shift_key(&key);

	unsigned int keycode = i8042_key(key);

	attr_value_t i = SIM_make_attr_integer(keycode);
	attr_value_t v = SIM_make_attr_integer(0); /* see i8042 docs */
	/* keycode value for shift found by trial and error :< */
	attr_value_t shift = SIM_make_attr_integer(72);

	set_error_t ret;

	/* press key */
	if (do_shift) {
		ret = SIM_set_attribute_idx(kbd, "key_event", &shift, &v);
		assert(ret == Sim_Set_Ok && "shift press failed!");
	}

	ret = SIM_set_attribute_idx(kbd, "key_event", &i, &v);
	assert(ret == Sim_Set_Ok && "cause_keypress press failed!");

	/* release key */
	v = SIM_make_attr_integer(1);
	ret = SIM_set_attribute_idx(kbd, "key_event", &i, &v);
	assert(ret == Sim_Set_Ok && "cause_keypress release failed!");

	if (do_shift) {
		ret = SIM_set_attribute_idx(kbd, "key_event", &shift, &v);
		assert(ret == Sim_Set_Ok && "cause_keypress release failed!");
	}
}

bool interrupts_enabled(conf_object_t *cpu)
{
	unsigned int eflags = GET_CPU_ATTR(cpu, eflags);
	return (eflags & EFL_IF) != 0;
}

static bool mem_translate(conf_object_t *cpu, unsigned int addr, unsigned int *result)
{
	if (KERNEL_MEMORY(addr)) {
		/* assume kern mem direct-mapped -- not strictly necessary */
		*result = addr;
		return true;
	} else if ((GET_CPU_ATTR(cpu, cr0) & CR0_PG) == 0) {
		/* paging disabled; cannot translate user address */
		return false;
	}

	unsigned int upper = addr >> 22;
	unsigned int lower = (addr >> 12) & 1023;
	unsigned int offset = addr & 4095;
	unsigned int cr3 = GET_CPU_ATTR(cpu, cr3);
	unsigned int pde_addr = cr3 + (4 * upper);
	unsigned int pde = SIM_read_phys_memory(cpu, pde_addr, WORD_SIZE);
	assert(SIM_get_pending_exception() == SimExc_No_Exception &&
	       "failed memory read during VM translation -- kernel VM bug?");
	/* check present bit of pde to not anger the simics gods */
	if ((pde & 0x1) == 0) {
		return false;
	}
	unsigned int pte_addr = (pde & ~4095) + (4 * lower);
	unsigned int pte = SIM_read_phys_memory(cpu, pte_addr, WORD_SIZE);
	assert(SIM_get_pending_exception() == SimExc_No_Exception &&
	       "failed memory read during VM translation -- kernel VM bug?");
	/* check present bit of pte to not anger the simics gods */
	if ((pte & 0x1) == 0) {
		return false;
	}
	*result = (pte & ~4095) + offset;
	return true;
}

unsigned int read_memory(conf_object_t *cpu, unsigned int addr, unsigned int width)
{
	unsigned int phys_addr;
	if (mem_translate(cpu, addr, &phys_addr)) {
		unsigned int result = SIM_read_phys_memory(cpu, phys_addr, width);
		assert(SIM_get_pending_exception() == SimExc_No_Exception &&
		       "failed memory read during VM translation -- kernel VM bug?");
		return result;
	} else {
		return 0; /* :( */
	}
}

char *read_string(conf_object_t *cpu, unsigned int addr)
{
	unsigned int length = 0;

	while (READ_BYTE(cpu, addr + length) != 0) {
		length++;
	}

	char *buf = MM_XMALLOC(length + 1, char);

	for (unsigned int i = 0; i <= length; i++) {
		buf[i] = READ_BYTE(cpu, addr + i);
	}

	return buf;
}

bool instruction_is_atomic_swap(conf_object_t *cpu, unsigned int eip) {
	unsigned int op = READ_BYTE(cpu, eip);
	if (op == 0xf0) {
		/* lock prefix */
		return instruction_is_atomic_swap(cpu, eip + 1);
	} else if (op == 0x86 || op == 0x87 || op == 0x90) {
		/* xchg */
		return true;
	} else if (op == 0x0f) {
		unsigned int op2 = READ_BYTE(cpu, eip + 1);
		if (op2 == 0xb0 || op2 == 0xb1) {
			/* cmpxchg */
			return true;
		} else {
			return false;
		}
	} else {
		return false;
	}
}

/* a similar trick to avoid timer interrupt, but delays by just 1 instruction. */
unsigned int delay_instruction(conf_object_t *cpu)
{
	/* Insert a relative jump, "e9 XXXXXXXX"; 5 bytes, just below stack. */
	unsigned int buf = GET_CPU_ATTR(cpu, esp) - 8;

	/* Translate buf's virtual location to physical address. */
	if (buf % PAGE_SIZE > PAGE_SIZE - 8) {
		buf -= 8;
	}
	unsigned int phys_buf;
	bool mapping_present = mem_translate(cpu, buf, &phys_buf);
	assert(mapping_present && "cannot delay instruction - stack not mapped!");

	/* Compute relative offset. Note "e9 00000000 would jump to buf+5. */
	unsigned int offset = GET_CPU_ATTR(cpu, eip) - (buf + 5);

	lsprintf(INFO, "Be back in a jiffy...\n");

	SIM_write_phys_memory(cpu, phys_buf, 0xe9, 1);
	SIM_write_phys_memory(cpu, phys_buf + 1, offset, 4);

	SET_CPU_ATTR(cpu, eip, buf);

	return buf;
}
