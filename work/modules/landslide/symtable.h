/**
 * @file symtable.h
 * @brief simics goo for querying the symbol table
 * @author Ben Blum
 */

#ifndef __LS_SYMTABLE_H
#define __LS_SYMTABLE_H

#include <simics/api.h>

conf_object_t *get_symtable();
void set_symtable(conf_object_t *symtable);
bool symtable_lookup(unsigned int eip, char **func, char **file, int *line);
// TODO: make new version for data version too
unsigned int symtable_lookup_data(char *buf, unsigned int maxlen, unsigned int addr);
bool function_eip_offset(unsigned int eip, unsigned int *offset);
bool find_user_global_of_type(const char *typename, unsigned int *size_result);

#endif
