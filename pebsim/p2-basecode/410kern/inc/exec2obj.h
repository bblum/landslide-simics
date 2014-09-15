/*
  Name: exec2obj.h
  Date: February 24th 2003
  Author: Steve Muckle

  Provides the interface to access user programs compiled as const char
  arrays.
*/

#ifndef _EXEC2OBJ_H
#define _EXEC2OBJ_H

#define MAX_EXECNAME_LEN    256
#define MAX_NUM_APP_ENTRIES 128

/* Format of entries in the table of contents. */
typedef struct {
  const char execname[MAX_EXECNAME_LEN];
  const char* execbytes;
  int execlen;
} exec2obj_userapp_TOC_entry;

/* The number of user executables in the table of contents. */
const int exec2obj_userapp_count;

/* The table of contents. */
const exec2obj_userapp_TOC_entry exec2obj_userapp_TOC[MAX_NUM_APP_ENTRIES];

#endif /* _EXEC2OBJ_H */
