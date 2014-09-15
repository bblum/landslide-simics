#include "410kern/inc/exec2obj.h"
#include <stdio.h>
#include <string.h>

#define STABS 0

void print_usage() {
  fprintf(stderr, "Usage: exec2obj <file>...\n");
  fprintf(stderr, "Creates a .s file containing a const char array for each file\n");
  fprintf(stderr, "given as an argument. Each array is initialized to the contents\n");
  fprintf(stderr, "of that file. A table of contents is also created. Look at\n");
  fprintf(stderr, "exec2obj.h for more information.\n\n");
}

char header[] =
#if STABS
"\t.file\t\"user_apps.c\"\n"
"\t.stabs\t\"/home/gghartma/15-410/proj2/\",100,0,0,.Ltext0\n"
"\t.stabs\t\"user_apps.c\",100,0,0,.Ltext0\n"
#endif
"\t.text\n"
".Ltext0:\n"
#if STABS
"\t.stabs\t\"gcc2_compiled.\",60,0,0,0\n"
"\t.stabs\t\"int:t(0,1)=r(0,1);-2147483648;2147483647;\",128,0,0,0\n"
"\t.stabs\t\"char:t(0,2)=r(0,2);0;127;\",128,0,0,0\n"
"\t.stabs\t\"long int:t(0,3)=r(0,3);-2147483648;2147483647;\",128,0,0,0\n"
"\t.stabs\t\"unsigned int:t(0,4)=r(0,4);0000000000000;0037777777777;\",128,0,0,0\n"
"\t.stabs\t\"long unsigned int:t(0,5)=r(0,5);0000000000000;0037777777777;\",128,0,0,0\n"
"\t.stabs\t\"long long int:t(0,6)=@s64;r(0,6);01000000000000000000000;0777777777777777777777;\",128,0,0,0\n"
"\t.stabs\t\"long long unsigned int:t(0,7)=@s64;r(0,7);0000000000000;01777777777777777777777;\",128,0,0,0\n"
"\t.stabs\t\"short int:t(0,8)=@s16;r(0,8);-32768;32767;\",128,0,0,0\n"
"\t.stabs\t\"short unsigned int:t(0,9)=@s16;r(0,9);0;65535;\",128,0,0,0\n"
"\t.stabs\t\"signed char:t(0,10)=@s8;r(0,10);-128;127;\",128,0,0,0\n"
"\t.stabs\t\"unsigned char:t(0,11)=@s8;r(0,11);0;255;\",128,0,0,0\n"
"\t.stabs\t\"float:t(0,12)=r(0,1);4;0;\",128,0,0,0\n"
"\t.stabs\t\"double:t(0,13)=r(0,1);8;0;\",128,0,0,0\n"
"\t.stabs\t\"long double:t(0,14)=r(0,1);12;0;\",128,0,0,0\n"
"\t.stabs\t\"complex int:t(0,15)=s8real:(0,1),0,32;imag:(0,1),32,32;;\",128,0,0,0\n"
"\t.stabs\t\"complex float:t(0,16)=R3;8;0;\",128,0,0,0\n"
"\t.stabs\t\"complex double:t(0,17)=R4;16;0;\",128,0,0,0\n"
"\t.stabs\t\"complex long double:t(0,18)=R5;24;0;\",128,0,0,0\n"
"\t.stabs\t\"void:t(0,19)=(0,19)\",128,0,0,0\n"
"\t.stabs\t\"__builtin_va_list:t(0,20)=*(0,2)\",128,0,0,0\n"
"\t.stabs\t\"_Bool:t(0,21)=@s8;-16\",128,0,0,0\n"
"\t.stabs\t\"user_apps.c\",130,0,0,0\n"
"\t.stabs\t\"410kern/lib/inc/exec2obj.h\",130,0,0,0\n"
"\t.stabs\t\"exec2obj_userapp_TOC_entry:t(2,1)=(2,2)=s264execname:(2,3)=ar(2,4)=r(2,4);0000000000000;0037777777777;;0;255;(2,5)=k(0,2),0,2048;execbytes:(2,6)=*(2,5),2048,32;execlen:(0,1),2080,32;;\",128,0,21,0\n"
"\t.stabn\t162,0,0,0\n"
#endif
"\t.section\t.rodata\n"
	  ;

void emit_file_header(FILE *s, const char *file, int execsize)
{
  fprintf(s,".globl %s_exec2obj_userapp_code_ptr\n", file);
  fprintf(s,"\t.align 32\n");
  fprintf(s,"\t.type\t%s_exec2obj_userapp_code_ptr, @object\n", file);
  fprintf(s,"\t.size\t%s_exec2obj_userapp_code_ptr, %d\n", file, execsize);
  fprintf(s,"%s_exec2obj_userapp_code_ptr:\n", file);
}

int emit_file_bytes(FILE *out, FILE *in, int bytes)
{
  char line[1];
  static const size_t sz = sizeof(line) / sizeof(line[0]);
  while (1) {
    size_t rval = fread(line, sizeof(line[0]), sz, in);
    if (rval <= 0)
      break;
    bytes -= rval;
    fprintf(out, "\t.byte\t%d", line[0]);
    char *it = line + 1;
    char *end = line + rval;
    while (it != end) {
      fprintf(out, ",%d", *it++);
    }
    fprintf(out, "\n");
  }
  return bytes;
}

void emit_dir_header(FILE *out, int nrfiles)
{
  fprintf(out, ".globl exec2obj_userapp_count\n"
	  "\t.align 4\n"
	  "\t.type\texec2obj_userapp_count, @object\n"
	  "\t.size\texec2obj_userapp_count, 4\n"
	  "exec2obj_userapp_count:\n");
  fprintf(out, "\t.long\t%d\n", nrfiles);
  fprintf(out, ".globl exec2obj_userapp_TOC\n"
	  "\t.align 32\n"
	  "\t.type\texec2obj_userapp_TOC, @object\n");
  fprintf(out, "\t.size\texec2obj_userapp_TOC, %d\n", sizeof(exec2obj_userapp_TOC));
  fprintf(out, "exec2obj_userapp_TOC:\n");
}

void emit_dir_entry(FILE *out, const char *name, int size)
{
  /* This is an awful hack, since we want the directory listing file
   * named something that it can't be named on the host file system
   * and can't appear in a symbol name. */
  const char *listed_name =
    (strcmp(name, "__DIR_LISTING__") == 0) ? "." : name;

  fprintf(out, "\t.string\t\"%s\"\n", listed_name);
  fprintf(out, "\t.zero\t%d\n", sizeof(exec2obj_userapp_TOC[0].execname) - strlen(listed_name) - 1);
  fprintf(out, "\t.long\t%s_exec2obj_userapp_code_ptr\n", name);
  fprintf(out, "\t.long\t%d\n", size);
}


void emit_dir_footer(FILE *out, int nrfiles)
{
  fprintf(out, "\t.zero\t%d\n",
	  sizeof(exec2obj_userapp_TOC) - nrfiles * sizeof(exec2obj_userapp_TOC[0]));
  fprintf(out,
#if STABS
	  "\t.stabs\t\"exec2obj_userapp_count:G(1,1)=k(0,1)\",32,0,8,0\n"
	  "\t.stabs\t\"exec2obj_userapp_TOC:G(1,2)=ar(2,4);0;127;(1,3)=k(2,1)\",32,0,9,0\n"
	  "\t.stabs\t\"idle_exec2obj_userapp_code_ptr:G(1,4)=ar(2,4);0;9574;(2,5)\",32,0,2,0\n"
	  "\t.stabs\t\"shell_exec2obj_userapp_code_ptr:G(1,5)=ar(2,4);0;39418;(2,5)\",32,0,4,0\n"
	  "\t.stabs\t\"init_exec2obj_userapp_code_ptr:G(1,6)=ar(2,4);0;21722;(2,5)\",32,0,6,0\n"
#endif
	  "\t.text\n"
#if STABS
	  "\t.stabs \"\",100,0,0,.Letext\n"
#endif
	  ".Letext:\n"
#if STABS
	  "\t.section\t.note.GNU-stack,\"\",@progbits\n"
	  "\t.ident\t\"GCC: (GNU) 3.3.5 (Debian 1:3.3.5-13)\"\n"
#endif
          );
}

#define MAX_FNAME 128

int main(int argc, char** argv) {
  FILE *in;
  int execsize = 0;
  int file_iter = 1;
  int execsizes[MAX_NUM_APP_ENTRIES];

  char fname_buf[MAX_FNAME];

  /* If there are no executables specified it's an error. */
  if (argc < 2) {
    print_usage();
    return -1;
  }

  if (argc > MAX_NUM_APP_ENTRIES + 1) {
    fprintf(stderr, "Too many executables:  The maximum is %d\n", MAX_NUM_APP_ENTRIES);
  }

  fwrite(header, sizeof(header[0]), sizeof(header) / sizeof(header[0]) - 1, stdout);

  while (file_iter < argc) {

    strncpy(fname_buf, argv[file_iter], MAX_FNAME);
    strncat(fname_buf, ".strip", MAX_FNAME - strlen(fname_buf));

    in = fopen(fname_buf,"r");
    if (!in) {
      fprintf(stderr, "Could not open %s for reading; falling back.\n",fname_buf);

      /* Open the executable. */
      in = fopen(argv[file_iter],"r");
      if (!in) {
        fprintf(stderr, "Could not open %s for reading.\n",argv[file_iter]);
        printf("\n\n.abort\n\n");
        return -1;
      }
    }

    /* Find size of executable. */
    fseek(in,0,SEEK_END);
    execsize = ftell(in);
    fseek(in,0,SEEK_SET);

    execsizes[file_iter] = execsize;


    /* Output the header of the file. */

    emit_file_header(stdout, argv[file_iter], execsize);
    emit_file_bytes(stdout, in, execsize);

    fclose(in);

    /* Move on to the next file. */
    file_iter++;
  }

  emit_dir_header(stdout, argc-1);

  /* Output the table of contents. */
  file_iter = 1;

  /* Output all other entries in TOC. */
  while(file_iter < argc) {
    emit_dir_entry(stdout, argv[file_iter], execsizes[file_iter]);
    file_iter++;
  }
  emit_dir_footer(stdout, argc-1);
  return 0;
}
