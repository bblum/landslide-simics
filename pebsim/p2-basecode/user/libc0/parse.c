#include <stdlib.h>
//#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <string.h>
#include <c0runtime.h>
#include "parse.h"

bool *parse_bool(c0_string s) {
  bool *result = NULL;
  const char *cstr = c0_string_tocstr(s);
  if (0 == strcmp(cstr, "true")) {
    result = c0_alloc(sizeof(bool));
    *result = true;
  } else if (0 == strcmp(cstr, "false")) {
    result = c0_alloc(sizeof(bool));
    *result = false;
  }
  c0_string_freecstr(cstr);
  return result;
}

int *parse_int(c0_string s, int base) {
  int *result = NULL;

  if (base < 2 || base > 36) c0_abort("parse_int: invalid base");

  const char *cstr = c0_string_tocstr(s);
  int errno = 0;
  char *endptr;
  long int li = strtol(cstr, &endptr, base);
  if (!isspace(cstr[0]) && cstr[0] != '+' /* strtol allows leading space or +,
                                             we don't -wjl */
      && errno == 0 && li >= INT_MIN && li <= INT_MAX && endptr != cstr
      && *endptr == '\0' /* make sure whole string was valid -wjl */) {
    result = c0_alloc(sizeof(int));
    *result = li;
  }
  c0_string_freecstr(cstr);
  return result;
}
