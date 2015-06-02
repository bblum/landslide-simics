#include <stdio.h>
#include <c0runtime.h>
#include <assert.h>
#include "file_util.h"
#include "conio_util.h"

#include <fake_io.h>

struct file {
  FILE *handle;
  bool isEOF;
};

typedef struct file* file_t;

bool peekEOF(FILE *f) 
//@requires f != NULL;
{
  if(EOF == fgetc(f)) 
    return true;
  fseek(f, -1, SEEK_CUR);
  return false;
}

file_t file_read(c0_string path) {
  const char* filename = c0_string_tocstr(path);
  file_t f = c0_alloc(sizeof(struct file));
  f->handle = fopen(filename, "r");
  if (!f->handle) return NULL;
  f->isEOF = peekEOF(f->handle);
  return f;
}

bool file_closed(file_t f) {
  assert(f != NULL);
  return f->handle == NULL;
}

void file_close(file_t f) {
  assert(f != NULL);
  assert(!file_closed(f));
  if (EOF == fclose(f->handle)) {
    c0_abort("Could not close file");
  }
  f->handle = NULL;
  return;
}

bool file_eof(file_t f) {
  assert(f != NULL);
  return f->isEOF;
}

c0_string file_readline(file_t f) {
  assert(f != NULL);
  assert(!file_closed(f));
  assert(!file_eof(f));
  if (feof(f->handle)) {
    c0_abort("At end of file, but file_eof returned false (bug)");
  }
  // From util.h in the conio library
  c0_string res = freadline(f->handle);
  f->isEOF = peekEOF(f->handle);
  return res;
}
