#include <string.h>
#include <stdlib.h>
#include "conio_util.h"

const size_t kInitReadBuffer = 64;

// Reads into a static buffer is possible, otherwise allocates on the heap
// TODO: make this gc-alloc efficiently
c0_string freadline(FILE *f) {
  char staticBuffer[kInitReadBuffer];
  char *buffer = staticBuffer;
  size_t maxBufferLength = kInitReadBuffer;
  size_t bufferSize = 0;

  int n;
  char c;
  while ((n = fgetc(f)) != '\n') {
    /* first, check for EOF or error condition */
    if (n == EOF) {
      if (feof(f)) {
        break;
      } else if (ferror(f)) {
        perror("error reading file");
        c0_abort("aborting due to file read error");
      } else {
        c0_abort("BUG: fgetc returned EOF, but !ferror and !feof... please report!");
      }
    }

    /* then, convert the int into a char and proceed as normal */
    c = n;

    if (c == '\r' || c == '\0')
      continue;

    if (bufferSize + 1 == maxBufferLength) {
      maxBufferLength *= 2;
      if (buffer == staticBuffer) {
        buffer = malloc(maxBufferLength);
        strncpy(buffer, staticBuffer, bufferSize);
      } else {
        buffer = realloc(buffer, maxBufferLength);
        if (!buffer)
          c0_abort("OOM in readline()");
      }
    }
    buffer[bufferSize++] = c;
  }

  buffer[bufferSize] = '\0';
  c0_string c0str = c0_string_fromcstr(buffer);
  if (buffer != staticBuffer)
    free(buffer);
  return c0str;
}

