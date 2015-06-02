#include <c0runtime.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

int string_length(c0_string s) {
  return c0_string_length(s);
}

char string_charat(c0_string s, int idx) {
  return c0_string_charat(s, idx);
}

c0_string string_join(c0_string a, c0_string b) {
  return c0_string_join(a, b);
}

c0_string string_sub(c0_string a, int start, int end) {
  return c0_string_sub(a, start, end);
}

bool string_equal(c0_string a, c0_string b) {
  return c0_string_equal(a, b);
}

int string_compare(c0_string a, c0_string b) {
  return c0_string_compare(a, b);
}

c0_string string_frombool(bool b) {
  return c0_string_fromliteral(b ? "true" : "false");
}

c0_string string_fromint(int i) {
  const size_t kBufSize = 16;
  char buffer[kBufSize];
  // This should always be false due to the limits of int32
  if (kBufSize == snprintf(buffer, kBufSize, "%d", i))
    c0_abort("Very unexpected error formatting integer");
  return c0_string_fromcstr(buffer);
}

c0_string string_fromchar(c0_char c) {
    char *res = c0_alloc(2);
    res[0] = c; /* XXX assumes type c0_char = char -wjl */
    res[1] = '\0';
    return c0_string_fromcstr(res);
}

/* return a lowercased version of s */
/* XXX maybe a bit roundabout to go through tocstr and fromcstr, but it keeps
   me from having to implement this once per runtime, and anyway, in both
   current runtimes (bare and c0rt) tocstr and fromcstr are essentially
   identities -wjl */
c0_string string_tolower(c0_string s) {
    const char *str = c0_string_tocstr(s);
    char *low = c0_alloc(strlen(str)+1);
    int i;
    for (i = 0; str[i] != '\0'; i++)
        low[i] = tolower(str[i]);
    low[i] = '\0';
    c0_string_freecstr(str);
    return c0_string_fromcstr(low);
}

bool string_terminated(c0_array A, int n) {
  int i;
  for (i = 0; i < n; i++) {
    if (*(c0_char*)c0_array_sub(A, i, sizeof(c0_char)) == '\0') return true;
  }
  return false;
}

c0_array string_to_chararray(c0_string s) {
  int len = c0_string_length(s); // does not include \0
  c0_array A = c0_array_alloc(sizeof(c0_char),len+1);
  const char *str = c0_string_tocstr(s);
  int i;
  for (i = 0; str[i] != '\0'; i++)
    *(c0_char*)c0_array_sub(A, i, sizeof(c0_char)) = str[i];
  *(c0_char*)c0_array_sub(A, i, sizeof(c0_char)) = '\0';
  c0_string_freecstr(str);
  return A;
}

c0_string string_from_chararray(c0_array A) {
  int len; char *cstr; int i;
  for (len = 0; *(c0_char*)c0_array_sub(A, len, sizeof(c0_char)) != '\0'; len++);
  cstr = c0_alloc(len+1);
  for (i = 0; i < len+1; i++)
    cstr[i] = *(c0_char*)c0_array_sub(A, i, sizeof(c0_char));
  return c0_string_fromcstr(cstr);
}

int char_ord(c0_char c) {
  return (int)c;
}

c0_char char_chr(int n) {
  if (0 > n || n > 127) c0_abort("character outside ASCII range (0..127)");
  return (c0_char)n;
}
