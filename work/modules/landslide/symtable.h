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
/* new version */
bool _symtable_lookup(int eip, char **func, char **file, int *line);
/* old version */
int symtable_lookup(char *buf, int maxlen, int addr, bool *unknown);
// TODO: make new version for data version too
int symtable_lookup_data(char *buf, int maxlen, int addr);
bool function_eip_offset(int eip, int *offset);
bool find_user_global_of_type(const char *typename, int *size_result);

#endif
