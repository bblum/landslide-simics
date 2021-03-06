/**
 * @file symtable.c
 * @brief A bunch of gross simics goo for querying the symbol table
 * @author Ben Blum
 */

#define MODULE_NAME "symtable glue"

#include "common.h"
#include "kspec.h"
#include "stack.h"
#include "symtable.h"
#include "x86.h"

#define SYMTABLE_NAME "deflsym"
#define CONTEXT_NAME "system.cell_context"

#define LIKELY_DIR "pebsim/"
#define UNKNOWN_FILE "410kern/boot/head.S"

#define GLOBAL_COLOUR        COLOUR_BOLD COLOUR_YELLOW
#define GLOBAL_INFO_COLOUR   COLOUR_DARK COLOUR_GREY

conf_object_t *get_symtable()
{
	conf_object_t *cell0_context = SIM_get_object(CONTEXT_NAME);
	if (cell0_context == NULL) {
		lsprintf(ALWAYS, "WARNING: couldn't get " CONTEXT_NAME "\n");
		return NULL;
	}
	attr_value_t table = SIM_get_attribute(cell0_context, "symtable");
	if (!SIM_attr_is_object(table)) {
		SIM_free_attribute(table);
		// ugh, wtf simics
		table = SIM_get_attribute(cell0_context, "symtable");
		if (!SIM_attr_is_object(table)) {
			SIM_free_attribute(table);
			assert(0 && CONTEXT_NAME ".symtable not an obj");
			return NULL;
		}
	}
	conf_object_t *symtable = SIM_attr_object(table);
	SIM_free_attribute(table);
	return symtable;
}

void set_symtable(conf_object_t *symtable)
{
	conf_object_t *cell0_context = SIM_get_object(CONTEXT_NAME);
	if (cell0_context == NULL) {
		lsprintf(ALWAYS, "WARNING: couldn't get cell0_context\n");
		return;
	}
	attr_value_t table = SIM_make_attr_object(symtable);
	assert(SIM_attr_is_object(table));
	set_error_t ret = SIM_set_attribute(cell0_context, "symtable", &table);
	assert(ret == Sim_Set_Ok && "set symtable failed!");
	SIM_free_attribute(table);
}

/* New interface. Returns malloced strings through output parameters,
 * which caller must free result strings if returnval is true */
bool symtable_lookup(unsigned int eip, char **func, char **file, int *line)
{
	conf_object_t *table = get_symtable();
	if (table == NULL) {
		return false;
	}

	attr_value_t idx = SIM_make_attr_integer(eip);
	attr_value_t result = SIM_get_attribute_idx(table, "source_at", &idx);
	if (!SIM_attr_is_list(result)) {
		SIM_free_attribute(idx);
		return false;
	}
	assert(SIM_attr_list_size(result) >= 3);

	/* Copy out the function name and line number. However, need to
	 * do some checks on the filename before copying it out as well. */
	if (testing_userspace() && eip == GUEST_CONTEXT_SWITCH_ENTER) {
		*func = MM_XSTRDUP("[context switch]");
#ifdef GUEST_HLT_EXIT
	} else if (testing_userspace() && eip == GUEST_HLT_EXIT) {
		*func = MM_XSTRDUP("[kernel idle]");
#endif
	} else {
		*func = MM_XSTRDUP(SIM_attr_string(SIM_attr_list_item(result, 2)));
	}
	const char *maybe_file = SIM_attr_string(SIM_attr_list_item(result, 0));
	*line = SIM_attr_integer(SIM_attr_list_item(result, 1));

	/* A hack to make the filenames shorter */
	if (strstr(maybe_file, LIKELY_DIR) != NULL) {
		maybe_file = strstr(maybe_file, LIKELY_DIR) + strlen(LIKELY_DIR);
	}

	/* The symbol table will claim that unknown assembly comes from
	 * 410kern/boot/head.S. Print an 'unknown' message instead. */
	if (strncmp(maybe_file, UNKNOWN_FILE, strlen(maybe_file)) == 0) {
		*file = NULL;
	} else {
		*file = MM_XSTRDUP(maybe_file);
	}

	SIM_free_attribute(result);
	SIM_free_attribute(idx);
	return true;
}

unsigned int symtable_lookup_data(char *buf, unsigned int maxlen, unsigned int addr)
{
	conf_object_t *table = get_symtable();
	if (table == NULL) {
		return scnprintf(buf, maxlen, GLOBAL_COLOUR "global0x%.8x"
				 COLOUR_DEFAULT, addr);
	}

	attr_value_t idx = SIM_make_attr_integer(addr);
	attr_value_t result = SIM_get_attribute_idx(table, "data_at", &idx);
	if (!SIM_attr_is_list(result)) {
		SIM_free_attribute(idx);
		if (KERNEL_MEMORY(addr)) {
			return scnprintf(buf, maxlen, "<user global0x%x>", addr);
		} else {
			return scnprintf(buf, maxlen, GLOBAL_COLOUR
					 "<kernel global0x%.8x>" COLOUR_DEFAULT, addr);
		}
	}
	assert(SIM_attr_list_size(result) >= 4);

	const char *globalname = SIM_attr_string(SIM_attr_list_item(result, 1));
	const char *typename = SIM_attr_string(SIM_attr_list_item(result, 2));
	unsigned int offset = SIM_attr_integer(SIM_attr_list_item(result, 3));

	unsigned int ret = scnprintf(buf, maxlen, GLOBAL_COLOUR "%s", globalname);
	if (offset != 0) {
		ret += scnprintf(buf+ret, maxlen-ret, "+%d", offset);
	}
	ret += scnprintf(buf+ret, maxlen-ret, GLOBAL_INFO_COLOUR
			 " (%s at 0x%.8x)" COLOUR_DEFAULT, typename, addr);

	SIM_free_attribute(result);
	SIM_free_attribute(idx);
	return ret;
}

/* Finds how many instructions away the given eip is from the start of its
 * containing function. */
bool function_eip_offset(unsigned int eip, unsigned int *offset)
{
	conf_object_t *table = get_symtable();
	if (table == NULL) {
		return false;
	}

	attr_value_t idx = SIM_make_attr_integer(eip);
	attr_value_t result = SIM_get_attribute_idx(table, "source_at", &idx);
	if (!SIM_attr_is_list(result)) {
		SIM_free_attribute(idx);
		return false;
	}
	assert(SIM_attr_list_size(result) >= 3);

	attr_value_t name = SIM_attr_list_item(result, 2);
	attr_value_t func = SIM_get_attribute_idx(table, "symbol_value", &name);

	*offset = eip - SIM_attr_integer(func);
	SIM_free_attribute(result);
	SIM_free_attribute(idx);
	return true;
}

/* Attempts to find an object of the given type in the global data region, and
 * learn its size. Writes to 'result' and returns true if successful. */
bool find_user_global_of_type(const char *typename, unsigned int *size_result)
{
#if defined(USER_DATA_START) && defined(USER_IMG_END)
	char *get_global_name_at(conf_object_t *table, unsigned int addr,
				 const char *typename)
	{
		attr_value_t idx = SIM_make_attr_integer(addr);
		attr_value_t result = SIM_get_attribute_idx(table, "data_at", &idx);
		if (!SIM_attr_is_list(result)) {
			SIM_free_attribute(idx);
			lsprintf(ALWAYS, "fail\n");
			return NULL;
		}
		assert(SIM_attr_list_size(result) >= 3);

		attr_value_t name = SIM_attr_list_item(result, 1);
		attr_value_t type = SIM_attr_list_item(result, 2);
		assert(SIM_attr_is_string(name));
		assert(SIM_attr_is_string(type));

		char *global_name;
		if (strncmp(SIM_attr_string(type), typename, strlen(typename)+1) == 0) {
			global_name = MM_XSTRDUP(SIM_attr_string(name));
		} else {
			global_name = NULL;
		}

		SIM_free_attribute(result);
		SIM_free_attribute(idx);
		return global_name;
	}

	conf_object_t *symtable = get_symtable();
	if (symtable == NULL) {
		return false;
	}
	// Look for a global object of type mutex_t in the symtable.
	unsigned int start_addr = USER_DATA_START;
	unsigned int last_addr = USER_IMG_END;
	unsigned int addr, end_addr;
	char *name = NULL;
	assert(start_addr % WORD_SIZE == 0);
	assert(last_addr % WORD_SIZE == 0);

	for (addr = start_addr; addr < last_addr; addr += WORD_SIZE) {
		if ((name = get_global_name_at(symtable, addr, typename)) != NULL) {
			break;
		}
	}
	if (addr == last_addr) {
		assert(name == NULL);
		return false;
	} else {
		assert(name != NULL);
	}
	// Ok, found a global mutex starting at addr.
	for (end_addr = addr + WORD_SIZE; end_addr < last_addr; end_addr += WORD_SIZE) {
		char *name2 = get_global_name_at(symtable, end_addr, typename);
		bool same_name = false;
		if (name2 != NULL) {
			same_name = strncmp(name2, name, strlen(name2)+1) == 0;
			MM_FREE(name2);
		}
		if (!same_name) {
			// Found end of our mutex.
			break;
		}
	}
	// Victory.
	lsprintf(DEV, "Found a %s named %s of size %d (%x to %x)\n",
		 typename, name, end_addr-addr, addr, end_addr);
	MM_FREE(name);
	*size_result = end_addr-addr;
	return true;
#else
	return false;
#endif
}
