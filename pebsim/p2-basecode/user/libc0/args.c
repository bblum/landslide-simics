#include <string.h>
#include <c0runtime.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
//#include <errno.h>
#include <assert.h>
#include <ctype.h>

/* lib/cc0main.c defined these */
extern int c0_argc;
extern char **c0_argv;

enum args_type {
  ARGS_BOOL, ARGS_INT, ARGS_STRING
};

struct args_list {
  const char *name;
  enum args_type tp;
  void *ptr;
  struct args_list *next;
};

struct args_list *args_list = NULL;

void args_flag(c0_string name, bool *ptr) {
  assert(ptr != NULL);
  struct args_list *l = c0_alloc(sizeof(struct args_list));
  l->name = c0_string_tocstr(name);
  l->tp = ARGS_BOOL;
  l->ptr = (void*) ptr;
  l->next = args_list;
  args_list = l;
}

void args_int(c0_string name, int *ptr) {
  assert(ptr != NULL);
  struct args_list *l = c0_alloc(sizeof(struct args_list));
  l->name = c0_string_tocstr(name);
  l->tp = ARGS_INT;
  l->ptr = (void*) ptr;
  l->next = args_list;
  args_list = l;
}

void args_string(c0_string name, c0_string *ptr) {
  assert(ptr != NULL);
  struct args_list *l = c0_alloc(sizeof(struct args_list));
  l->name = c0_string_tocstr(name);
  l->tp = ARGS_STRING;
  l->ptr = (void*) ptr;
  l->next = args_list;
  args_list = l;
}

struct args_list *args_get(const char *name) {
  struct args_list *l = args_list;
  while (l != NULL) {
    if (!strcmp(l->name, name))
      return l;
    l = l->next;
  }
  return NULL;
}

struct args {
  int argc;
  c0_array argv;		/* string[] */
};
typedef struct args* args_t;

bool get_int (const char *arg, int *ans) {
  int errno = 0;
  char *endptr;
  long int li = strtol(arg, &endptr, 10);
  if (errno == 0 && !isspace(*arg) && *arg != '+' /* don't allow leading space or + */
      && INT_MIN <= li && li <= INT_MAX		  /* ensure in range for int */
      && endptr != arg && *endptr == '\0') { /* ensure whole string is parsed */
    *ans = (int)li;
    return true;
  } else {
    return false;
  }
}

args_t args_parse() {
  int argc = c0_argc-1;		/* set in main */
  char **argv = c0_argv+1;	/* set in main */
  int other_count = 0;
  struct args_list* other_list = NULL;
  while (argc > 0) {
    struct args_list *l = args_get(*argv);
    if (l == NULL) {
      /* not a recognized argument - add to "others" */
      other_count++;
      struct args_list* o = c0_alloc(sizeof(struct args_list));
      o->name = *argv;
      o->tp = ARGS_STRING;
      /* o->ptr = NULL; */ /* unused */
      o->next = other_list;
      other_list = o;
      argc--; argv++;
    } else {
      argc--; argv++;
      switch (l->tp) {
      case ARGS_BOOL:
	*(bool*)l->ptr = true;
	break;
      case ARGS_INT:
        if(argc == 0) return NULL;
        int ans;
	if(!get_int(*argv, &ans)) return NULL;
	*(int*)l->ptr = ans;
	argc--; argv++;
	break;
      case ARGS_STRING:
        if(argc == 0) return NULL;
	*(c0_string*)l->ptr = c0_string_fromcstr(*argv);
	argc--; argv++;
	break;
      }
    }
  }

  c0_array args_array = c0_array_alloc(sizeof(c0_string), other_count);
  /* reverse linked list order to obtain command line order */
  int i;
  for (i = other_count-1; 0 <= i; i--) {
    *(c0_string*)c0_array_sub(args_array, i, sizeof(c0_string)) =
      c0_string_fromcstr(other_list->name);
    other_list = other_list->next;
  }

  struct args *args = (struct args *)c0_alloc(sizeof(struct args));
  args->argc = other_count;
  args->argv = args_array;

  return args;
}
