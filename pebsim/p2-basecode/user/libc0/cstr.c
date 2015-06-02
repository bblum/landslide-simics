#include <string.h>
#include <c0runtime.h>

bool c0_char_equal(c0_char a, c0_char b) {
  return a == b;
}

int c0_char_compare(c0_char a, c0_char b) {
  return a - b;
}

c0_string c0_string_empty() {
  return "";
}

int c0_string_length(c0_string s) {
  char* str = (char*)s;
  return str ? strlen(str) : 0;
}

bool c0_string_equal(c0_string a, c0_string b) {
  return 0 == c0_string_compare(a,b);
}

int c0_string_compare(c0_string a, c0_string b) {
  char* astr = a ? (char*)a : "";
  char* bstr = b ? (char*)b : "";
  int res = strcmp(astr,bstr);
  return (res < 0) ? -1 : ((res > 0) ? 1 : 0);
}

c0_char c0_string_charat(c0_string s, int i) {
  int len = c0_string_length(s);
  if (!(0 <= i && i < len)) c0_abort("c0_string_charat: index out of bounds");
  char* str = (char*)s;
  return str[i];
}

c0_string c0_string_sub(c0_string s, int start, int end) {
  char* str = (char*) s;
  if (0 > start) c0_abort("c0_string_sub: 0 > start");
  if (start > end) c0_abort("c0_string_sub: start > end");

  if (!str) {
    if (end > 0) c0_abort("c0_string_sub: end > length of string");
    return "";
  }

  size_t len = strlen(str);
  if (end > (int)len) c0_abort("c0_string_sub: end > length of string");

  //@assert 0 <= start && start <= end && end <= len;
  if (start == end) return "";
  size_t sublen = end - start;
  char *sub = c0_alloc(sublen+1);
  sub[sublen] = '\0';
  strncat(sub, str+start, sublen);
  return (c0_string)sub;
}

c0_string c0_string_join(c0_string a, c0_string b) {
  if (!a) return b;
  if (!b) return a;

  char *c = c0_alloc(strlen((char*)a) + strlen((char*)b) + 1);
  strcpy(c, a);
  strcat(c, b);
  return (c0_string)c;
}

c0_string c0_string_fromcstr(const char *s) {
  char *c = c0_alloc(strlen(s) + 1);
  strcpy(c, s);
  return (c0_string)c;
}

c0_string c0_string_fromliteral(const char *s) {
  return (c0_string)s;
}

const char *c0_string_tocstr(c0_string s) {
  return s ? s : "";
}

void c0_string_freecstr(const char *s) {
  (void) *s;
  /* s = s; */ /* silence compiler */
}

