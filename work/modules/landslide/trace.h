/*
  trace.h

  Copyright 1998-2009 Virtutech AB

  The contents herein are Source Code which are a subset of Licensed
  Software pursuant to the terms of the Virtutech Simics Software
  License Agreement (the "Agreement"), and are being distributed under
  the Agreement.  You should have received a copy of the Agreement with
  this Licensed Software; if not, please contact Virtutech for a copy
  of the Agreement prior to using this Licensed Software.

  By using this Source Code, you agree to be bound by all of the terms
  of the Agreement, and use of this Source Code is subject to the terms
  the Agreement.

  This Source Code and any derivatives thereof are provided on an "as
  is" basis.  Virtutech makes no warranties with respect to the Source
  Code or any derivatives thereof and disclaims all implied warranties,
  including, without limitation, warranties of merchantability and
  fitness for a particular purpose and non-infringement.

*/

#ifndef __LS_TRACE_H
#define __LS_TRACE_H

typedef enum {
        TR_Reserved = 0, TR_Data = 1, TR_Instruction = 2, TR_Exception = 3,
        TR_Execute = 4, TR_Reserved_2, TR_Reserved_3, TR_Reserved_4,
        TR_Reserved_5, TR_Reserved_6, TR_Reserved_7, TR_Reserved_8,
        TR_Reserved_9, TR_Reserved_10, TR_Reserved_11, TR_Reserved_12
} trace_type_t;

typedef enum {
        TA_generic, TA_x86, TA_ia64, TA_v9
} trace_arch_t;

typedef struct {
        unsigned arch:2;
        unsigned trace_type:4;	        /* data, instruction, or exception (trace_type_t) */
        signed   cpu_no;		/* processor number (0 and up) */
        uint32   size;
        unsigned read_or_write:1;	/* read_or_write_t */

        /* x86 */
        unsigned linear_access:1;       /* if linear access */
        unsigned seg_reg:3;             /* 0-5, with ES=0, CS=1, SS=2, DS=3, FS=4, GS=5 */
        linear_address_t la;
        x86_memory_type_t memory_type;

        /* x86_access_type_t or sparc_access_type_t */
        int access_type;

        /* virtual and physical address of effective address */
        logical_address_t va;
        physical_address_t pa;

        int atomic;

        union {
                uint64 data;            /* if TR_Date */
                uint8  text[16];        /* if TR_Instruction or TR_Execute
                                           (large enough for any target) */
                int exception;		/* if TR_Exception */
        } value;

        cycles_t timestamp;             /* Delta coded */
} trace_entry_t;

/* ADD INTERFACE trace_consume_interface */
typedef struct {
        void (*consume)(conf_object_t *obj, trace_entry_t *entry);
} trace_consume_interface_t;

#define TRACE_CONSUME_INTERFACE "trace_consume"

#endif
