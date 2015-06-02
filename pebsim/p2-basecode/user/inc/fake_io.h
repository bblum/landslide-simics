#ifndef __FAKE_IO_H__
#define __FAKE_IO_H__

#include <stdio.h>
#include <assert.h>

typedef int FILE;

/* for cc0main.c */

#define fopen(filename, mode) ({ (void)(filename); ((FILE *)0xb0b0b0b0); })
#define fwrite(ptr, size, nitems, file) (nitems)
#define fclose(file) 0

#ifndef stderr
#define stderr ((FILE *)0x1badd00d)
#endif
#ifndef stdout
#define stdout ((FILE *)0x1badbabe)
#endif
#ifndef stdin
#define stdin ((FILE *)0x1badfee1)
#endif

#define getenv(name) (NULL)

#define fprintf(file, ...) do {				\
		FILE *__file = (file);				\
		if (__file == stderr || __file == stdout) {	\
			printf(__VA_ARGS__);			\
		}						\
	} while (0)

/* for file.c */

#ifndef EOF
#define EOF (-1)
#endif

#define fgetc(file) EOF
#define fseek(file, offset, mode) do { } while (0)
#define feof(file) 1

#define perror printf

/* for conio.c */

#define fflush(file) do { } while (0)

/* for file_util.c */

#define ferror(file) 0

/* for test programs */

#define EXIT_FAILURE 1
#define EXIT_SUCCESS 0

/* for bare */

#define raise(signal) assert(0 && "crash with signal " #signal)
#define SIGSEGV 11
#define SIGABRT 6
#define SIGFPE 8

#endif
