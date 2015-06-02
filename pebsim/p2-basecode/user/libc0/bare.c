#include <stdlib.h>
#include <stdio.h>
//#include <signal.h>
#include <limits.h>
#include <string.h>
#include <c0runtime.h>

void c0_runtime_init() {
  // nothing to do for the bare runtime
}

void c0_runtime_cleanup() {
  // nothing to do for the bare runtime
}

void raise_msg(int signal, const char* msg) {
  fprintf(stderr, "%s\n", msg);
  fflush(stderr);
  raise(signal);
}

void c0_error(const char *msg) {
  fprintf(stderr, "Error: %s\n", msg);
  fflush(stderr);
  exit(EXIT_FAILURE);
}

void c0_abort(const char *reason) {
  raise_msg(SIGABRT, reason);
}


/* Arithmetic */

void c0_abort_arith(const char *reason) {
  raise_msg(SIGFPE, reason);
}

c0_int c0_idiv(c0_int x, c0_int y) {
  if(y == 0) c0_abort_arith("division by 0");
  if(y == -1 && x == INT_MIN) c0_abort_arith("division causes overflow");
  return x/y;
}

c0_int c0_imod(c0_int x, c0_int y) {
  if(y == 0) c0_abort_arith("modulo by 0");
  if(y == -1 && x == INT_MIN) c0_abort_arith("modulo causes overflow");
  return x%y;  
}

c0_int c0_sal(c0_int x, c0_int y) {
  if(y < 0 || y >= 32) c0_abort_arith("shift left out-of-range");
  return x<<y;
}

c0_int c0_sar(c0_int x, c0_int y) {
  if(y < 0 || y >= 32) c0_abort_arith("shift right out-of-range");
  return x>>y;
}


/* Memory */

/* Arrays are represented as EITHER a null pointer or an array with
 * three fields: count, elt_size, and elems.  Elems is a void pointer
 * pointing to the array data.
 * 
 * In fact, this pointer always actually points one past the end of
 * the struct:       _
 *                  / \
 * |---------------/---v------...
 * |    |    |    *   |       ...
 * |--------------------------...
 *
 * It would be incorrect for an implementation to rely on this
 * behavior. */

struct c0_array_header {
  c0_int count;
  c0_int elt_size;
  void* elems;
};

void c0_abort_mem(const char *reason) {
  raise_msg(SIGSEGV, reason);
}

c0_pointer c0_alloc(size_t elt_size) {
  // Require non-zero allocation so that alloc acts as a gensym
  if (elt_size == 0) elt_size = 1;
  void *p = calloc(1, elt_size);
  if (p == NULL) c0_abort_mem("allocation failed");
  return (void *)p;
}

void* c0_deref(c0_pointer a){
  if (a == NULL) c0_abort_mem("attempt to dereference null pointer");
  return a;
}

c0_array c0_array_alloc(size_t elemsize, c0_int elemcount) {
  /* test for overflow, somehow? */
  if (elemcount < 0) c0_abort_mem("array size cannot be negative");
  if (elemsize > 0 && (unsigned int)elemcount > ((1<<30)-8)/elemsize)
    c0_abort_mem("array size too large");

  c0_array p = calloc(1, sizeof(struct c0_array_header) + elemcount*elemsize);
  if (p == NULL) c0_abort_mem("array allocation failed");
  p->count = elemcount;              /* initialize number of elements */
  p->elt_size = elemsize;            /* store element size */
  p->elems = (void*)(p+1);           /* initalize pointer to memory immediately
                                        following the struct */
  return p;
}

void* c0_array_sub(c0_array A, int i, size_t elemsize) {
  (void) elemsize;              /* silence compiler */
  if (A == NULL) c0_abort_mem("attempt to access default zero-size array");
  if (((unsigned)i) >= (unsigned)(A->count))
    c0_abort_mem("array index out of bounds");
  return (void *) (((char*)A->elems) + i*A->elt_size);
}

c0_int c0_array_length(c0_array A) {
  return A ? A->count : 0;
}

struct c0_tagged_struct {
  char* tyrep;
  void* ptr;
};

c0_tagged_ptr c0_tag_ptr(char* tyrep, c0_pointer a) {
  if (a == NULL)
    return NULL;
  else {
    c0_tagged_ptr p = malloc(sizeof(struct c0_tagged_struct));
    if (!p) c0_abort_mem("allocation failed");
    p->tyrep = tyrep;
    p->ptr = a;
    return p;
  }
}

void* c0_untag_ptr(char* tyrep, c0_tagged_ptr p) {
  if (p == NULL)
    return NULL;
  else if (strcmp(tyrep, p->tyrep) == 0)
    return p->ptr;
  else
    c0_abort_mem("untagging pointer failed");
  return NULL; /* should be unreachable */
}

/* we don't compare tags since pointers with different
 * tags cannot be equal anyway */
c0_bool c0_tagged_eq(c0_tagged_ptr p, c0_tagged_ptr q) {
  void* raw_p = (p == NULL) ? p : p->ptr;
  void* raw_q = (q == NULL) ? q : q->ptr;
  return raw_p == raw_q;
}

c0_bool c0_hastag(char* tyrep, c0_tagged_ptr p) {
  if (p == NULL)
    return true;
  else
    return strcmp(tyrep, p->tyrep) == 0;
}
