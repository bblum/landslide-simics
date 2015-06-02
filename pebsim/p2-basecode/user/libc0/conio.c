#include <stdio.h>
#include <c0runtime.h>
#include "conio_util.h"
#include <fake_io.h>

void print(c0_string s) {
  const char *cstr = c0_string_tocstr(s);
  printf("%s", cstr);
  c0_string_freecstr(cstr);
}

void println(c0_string s) {
  const char *cstr = c0_string_tocstr(s);
  puts(cstr);
  c0_string_freecstr(cstr);
}

void flush() {
  fflush(stdout);
}

void printint(int i) {
  printf("%d", i);
}

void printbool(bool b) {
  puts(b ? "true" : "false");
}

void printchar(char c) {
  putchar(c);
}

c0_string readline() {
  return freadline(stdin);
}

/* Added Sep 27, 2012 -fp */
bool eof() {
  return (bool)(feof(stdin) ? 1 : 0);
}
