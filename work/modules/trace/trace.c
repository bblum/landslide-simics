/*
  trace.c

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

/*
  The trace facility will generate a sequence of function calls to a
  user-defined trace consumer. This is done for instruction fetches,
  data accesses, and exceptions. It is implemented by hooking in a
  memory-hierarchy into Simics and installing an hap handler that
  catches exceptions.

  The data passed is encoded in a trace_entry_t data structure and is
  on a "will execute" basis. The trace consumer is presented with a
  sequence of instructions that are about to execute, not that have
  executed. Thus, register contents are those that are in place prior
  to the instruction is executed. Various conditions will
  stop the instruction from executing, primarly exceptions -
  these generate new trace entries. Memory instructions that are
  executed will also generate memory access trace entries. Memory
  instructions that cause a fault will generate a memory access trace
  entry upon correct execution, typically after one or more fault
  handlers have executed.
*/

/*
  <add id="simics generic module short">
  <name>Trace</name><ndx>trace</ndx>
  <ndx>memory traces</ndx><ndx>instruction traces</ndx>
  This module provides an easy way of generating traces from Simics.
  Actions traced are executed instructions, memory accesses and,
  occurred exceptions. Traces will by default be printed as text to the
  terminal but can also be directed to a file in which case a binary
  format is available as well.

  The data presented is on a "will execute" basis. The trace will
  contain a sequence of instructions that are about to execute, not
  that have executed. Thus, register contents are those that are in
  place prior to the instruction is executed. Various conditions will
  stop the instruction from executing, primarly exceptions &mdash;
  these generate new trace entries. Memory instructions that are
  executed will generate memory access trace entries. Memory
  instructions that cause a fault will generate a memory access trace
  entry upon correct execution, typically after one or more fault
  handlers have executed.

  See the <i>trace</i> section of the <i>Commands</i> chapter in the
  <i>Simics Reference Manual</i> for the available trace
  commands. Also look at the documentation for the class base_trace
  that have some attributes that can be set to control what will be
  traced.

  </add> */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <simics/api.h>
#include <simics/alloc.h>
#include <simics/utils.h>
#include <simics/arch/sparc.h>
#include <simics/arch/x86.h>

#if defined(HAVE_LIBZ)
 #include <zlib.h>
 #define GZ_FILE(bt) ((bt)->gz_file)
 #define GZ(x)       (x)
#else
 #define GZ_FILE(bt) NULL
 #define GZ(x)
#endif

#include "trace.h"

/* Cached information about a processor. */
typedef struct {
        unsigned va_digits;
        unsigned pa_digits;
        conf_object_t *cpu;
        char name[10];
        processor_info_interface_t *info_iface;
        exception_interface_t *exception_iface;
} cpu_cache_t;

typedef struct {
        generic_address_t start;
        generic_address_t end;
} interval_t;
typedef VECT(interval_t) interval_list_t;

enum { PHYSICAL, VIRTUAL, NUM_ADDRESS_TYPES };

typedef struct base_trace {
        log_object_t log;

        /*
         * Scratch area used for communicating trace event data. This is
         * maintained on a per processor basis.
         */
        trace_entry_t current_entry;
        trace_entry_t last_entry;

        /* Trace to file if file_name is non-NULL. */
        char *file_name;
        int warn_on_existing_file;
        FILE *file;
#if defined(HAVE_LIBZ)
        gzFile gz_file;
#endif

        /* Count the events of each type. */
        uint64 exec_count;
        uint64 data_count;
        uint64 exc_count;

        /* 0 for text, 1 for raw */ 
        int trace_format;   

        conf_object_t *consumer;
        trace_consume_interface_t consume_iface;

        /* Cached processor info. */
        cpu_cache_t *cpu;

        /* "Processor info" for non-cpu devices. */
        cpu_cache_t device_cpu;

        /* True if we are currently hooked into the memory hierarchy, false
           otherwise. */
        int memhier_hook;

        int trace_enabled;

        int trace_exceptions;
        int trace_instructions;
        int trace_data;
        int filter_duplicates;

        int print_physical_address;
        int print_virtual_address;
        int print_linear_address;
        int print_access_type;
        int print_memory_type;
        int print_data;

        /* Lists of intervals to trace data accesses for. _stc_ are the same
           lists, butrounded outwards to DSTC block size. */
        interval_list_t data_interval[NUM_ADDRESS_TYPES];
        interval_list_t data_stc_interval[NUM_ADDRESS_TYPES];

        cycles_t last_timestamp;

        /*
         * Function pointer to the trace consumer (if there is one). In future
         * we may want to support multiple trace consumers, especially if we do
         * proper statistical sampling of various characteristics, in which
         * case to reduce unwanted covariance we want separate sampling, with
         * this facility capable of handling overlaps.
         */
        void (*trace_consume)(struct base_trace *bt, trace_entry_t *);

#if defined(TRACE_STATS)
        uint64 instruction_records;
        uint64 data_records;
        uint64 other_records;
#endif   /* TRACE_STATS */
} base_trace_t;


typedef struct trace_mem_hier_object {
        conf_object_t obj;
        base_trace_t *bt;

        /* For forwarding requests. */
        conf_object_t *timing_model;
        timing_model_interface_t timing_iface;
        conf_object_t *snoop_device;
        timing_model_interface_t snoop_iface;
} trace_mem_hier_object_t;

static const char *const read_or_write_str[] = { "Read ", "Write"};

const char *const seg_regs[] = {"es", "cs", "ss", "ds", "fs", "gs", 0};

static void
external_tracer(base_trace_t *bt, trace_entry_t *ent)
{
        bt->consume_iface.consume(bt->consumer, ent);
}

static void
text_trace_data(base_trace_t *bt, trace_entry_t *ent, char *s)
{
        cpu_cache_t *cc;

        if (ent->cpu_no == -1)
                cc = &bt->device_cpu;
        else
                cc = &bt->cpu[ent->cpu_no];
        bt->data_count++;
        /* vtsprintf works like sprintf but allows the "ll" size modifier
           to be used for long long (64-bit integers) even on Windows,
           which isn't possible with the sprintf in Microsoft's C library. */
        s += vtsprintf(s, "data: [%9llu] %s", bt->data_count, cc->name);

        if (ent->arch == TA_x86 && bt->print_linear_address)
                s += vtsprintf(s, "<l:0x%0*llx> ", cc->va_digits, ent->la);
        if (bt->print_virtual_address) {
                if (ent->arch == TA_x86)
                        if (ent->linear_access) {
                                s += vtsprintf(s, "<linear access> ");
                        } else {
                                s += vtsprintf(s, "<%s:0x%0*llx> ",
                                               seg_regs[ent->seg_reg],
                                               cc->va_digits,
                                               (unsigned long long) ent->va);
                        }
                else
                        s += vtsprintf(s, "<v:0x%0*llx> ", cc->va_digits,
                                       ent->va);
        }
        if (bt->print_physical_address)
                s += vtsprintf(s, "<p:0x%0*llx> ", cc->pa_digits, ent->pa);

        if (ent->arch == TA_v9 && bt->print_access_type) {
                const char *str;
                switch (ent->access_type) {
                case V9_Access_Normal:        str = "Nrml" ; break;
                case V9_Access_Normal_FP:     str = "FP" ; break;
                case V9_Access_Double_FP:     str = "Lddf/Stdf" ; break;
                case V9_Access_Short_FP:      str = "ShortFP" ; break;
                case V9_Access_FSR:           str = "FSR" ; break;
                case V9_Access_Atomic:        str = "Atomic" ; break;
                case V9_Access_Atomic_Load:   str = "AtomicLD" ; break;
                case V9_Access_Prefetch:      str = "Prefech" ; break;
                case V9_Access_Partial_Store: str = "PStore" ; break;
                case V9_Access_Ldd_Std_1:     str = "Ldd/Std1" ; break;
                case V9_Access_Ldd_Std_2:     str = "Ldd/Std2" ; break;
                case V9_Access_Block:         str = "Block" ; break;
                case V9_Access_Internal1:     str = "Intrn1" ; break;
                default:                      str = "Unknwn" ; break;
                }
                s += vtsprintf(s, "%s ", str);
        }

        if (ent->arch == TA_x86 && bt->print_access_type) {
                const char *str = "*er*";
                switch (ent->access_type) {
                case X86_Other: str = "Othr"; break;
                case X86_Vanilla: str = "Vani"; break;
                case X86_Instruction: str = "Inst"; break;
                case X86_Clflush: str = "Clfl"; break;
                case X86_Fpu_Env: str = "Fenv"; break;
                case X86_Fpu_State: str = "Fste"; break;
                case X86_Idt: str = "Idt "; break;
                case X86_Gdt: str = "Gdt "; break;
                case X86_Ldt: str = "Ldt "; break;
                case X86_Task_Segment: str = "Tseg"; break;
                case X86_Task_Switch: str = "Tswi"; break;
                case X86_Far_Call_Parameter: str = "CPar"; break;
                case X86_Stack: str = "Stac"; break;
                case X86_Pml4: str = "Pml4"; break;
                case X86_Pdp: str = "Pdp "; break;
                case X86_Pd: str = "Pd  "; break;
                case X86_Pt: str = "Pt  "; break;
                case X86_Sse: str = "Sse "; break;
                case X86_Fpu: str = "Fpu "; break;
                case X86_Access_Simple: str = "AccS"; break;
                case X86_Microcode_Update: str = "UCP "; break;
                case X86_Non_Temporal: str = "NTmp"; break;
                case X86_Prefetch_3DNow: str = "Pr3 "; break;
                case X86_Prefetchw_3DNow: str = "Prw3"; break;
                case X86_Prefetch_T0: str = "PrT0"; break;
                case X86_Prefetch_T1: str = "PrT1"; break;
                case X86_Prefetch_T2: str = "PrT2"; break;
                case X86_Prefetch_NTA: str = "PrNT"; break;
                case X86_Loadall: str = "Lall"; break;
                case X86_Atomic_Info: str = "AInf"; break;
                case X86_Cmpxchg16b: str = "X16B"; break;
                case X86_Smm_State: str = "SMM "; break;
                case X86_Vmcs: str = "VMCS"; break;
                case X86_Vmx_IO_Bitmap: str = "VMX "; break;
                case X86_Vmx_Vapic: str = "VMX "; break;
                case X86_Vmx_Msr: str = "VMX "; break;
                }
                s += vtsprintf(s, "%s ", str);

                if (ent->atomic)
                        s += vtsprintf(s, "atomic ");
        }
        if (ent->arch == TA_x86 && bt->print_memory_type) {
                const char *str = "er";
                switch (ent->memory_type) {
                case X86_None:
                        break;
                case X86_Strong_Uncacheable:    /* UC */
                case X86_Uncacheable:           /* UC- */
                        str = "UC";
                        break;
                case X86_Write_Combining:
                        str = "WC";
                        break;
                case X86_Write_Through:
                        str = "WT";
                        break;
                case X86_Write_Back:
                        str = "WB";
                        break;
                case X86_Write_Protected:
                        str = "WP";
                        break;
                }
                s += vtsprintf(s, "%s ", str);
        }

        s += vtsprintf(s, "%s %2d bytes  ",
                       read_or_write_str[ent->read_or_write], ent->size);
        if (bt->print_data)
                s += vtsprintf(s, "0x%llx", ent->value.data);
        strcpy(s, "\n");
}

static void
text_trace_exception(base_trace_t *bt, trace_entry_t *ent, char *s)
{
        cpu_cache_t *cc;

        if (ent->cpu_no == -1)
                cc = &bt->device_cpu;
        else
                cc = &bt->cpu[ent->cpu_no];
        bt->exc_count++;
        s += vtsprintf(s, "exce: [%9llu] %s", bt->exc_count, cc->name);
        s += vtsprintf(s, "exception %3d (%s)\n",
                       ent->value.exception,
                       cc->exception_iface->get_name(cc->cpu,
                                                     ent->value.exception));
        SIM_clear_exception();
}

#define EXEC_VA_PREFIX(ent) ((ent)->arch == TA_x86 ? "cs" : "v")

static void
text_trace_instruction(base_trace_t *bt, trace_entry_t *ent, char *s)
{
        cpu_cache_t *cc;

        /* CPU number, addresses. */
        cc = &bt->cpu[ent->cpu_no];
        bt->exec_count++;
        s += vtsprintf(s, "inst: [%9llu] %s", bt->exec_count, cc->name);
        if (ent->arch == TA_x86 && bt->print_linear_address)
                s += vtsprintf(s, "<l:0x%0*llx> ", cc->va_digits, ent->la);
        if (bt->print_virtual_address)
                s += vtsprintf(s, "<%s:0x%0*llx> ",
                               EXEC_VA_PREFIX(ent), cc->va_digits, ent->va);
        if (bt->print_physical_address)
                s += vtsprintf(s, "<p:0x%0*llx> ", cc->pa_digits, ent->pa);

        /* Opcode. */
        if (bt->print_data) {
                int i;
                if (ent->arch == TA_x86) {
                        /* x86 has variable length opcodes. Pad with spaces to
                           6 bytes to keep the columns aligned for all but the
                           longest opcodes. */
                        for (i = 0; i < ent->size; i++)
                                s += vtsprintf(s, "%02x ",
                                               (uint32) ent->value.text[i]);
                        for (i = ent->size; i < 6; i++)
                                s += vtsprintf(s, "   ");
                } else {
                        for (i = 0; i < ent->size; i++)
                                s += vtsprintf(s, "%02x",
					     (uint32) ent->value.text[i]);
                        s += vtsprintf(s, " ");
                }
        }

        /* Disassembly. */
        attr_value_t opcode = SIM_make_attr_data(ent->size, ent->value.text);
        tuple_int_string_t ret;
        int sub_operation;
        if (ent->arch == TA_ia64)
                sub_operation = (ent->va & 0xF);
        else
                sub_operation = 0;
        ret = cc->info_iface->disassemble(cc->cpu, ent->va, opcode, sub_operation);
        if (ret.string) {
                s += vtsprintf(s, "%s", ret.string);
                MM_FREE((void *)ret.string);
        } else {
                s += vtsprintf(s, "(*** No disassembly provided ***)");
        }

        strcpy(s, "\n");
}

static void
text_tracer(base_trace_t *bt, trace_entry_t *ent)
{
	/* With all options and longest opcodes, the lenght is around 200 */
        char s[8192];

        switch (ent->trace_type) {
        case TR_Data:
                text_trace_data(bt, ent, s);
                break;
        case TR_Exception:
                text_trace_exception(bt, ent, s);
                break;
        case TR_Instruction:
                text_trace_instruction(bt, ent, s);
                break;
        case TR_Reserved:
        default:
                strcpy(s, "*** Trace: unknown trace event type.\n");
                break;
        }

	/* Catch errors that produce unreasonable long lines */
	ASSERT(strlen(s) < 500);
	
        if (bt->file != NULL) {
                fputs(s, bt->file);
        } else if (GZ_FILE(bt) != NULL) {
                GZ(gzwrite(GZ_FILE(bt), s, strlen(s)));
        } else {
                SIM_write(s, strlen(s));
        }
}

static void
raw_tracer(base_trace_t *bt, trace_entry_t *ent)
{
        if (bt->file != NULL) {
                fwrite(ent, sizeof(trace_entry_t), 1, bt->file);
        } else if (GZ_FILE(bt) != NULL) {
                GZ(gzwrite(GZ_FILE(bt), ent, sizeof(trace_entry_t)));
        }
}

#if defined(TRACE_STATS)
static void
stats_tracer(base_trace_t *bt, trace_entry_t *ent)
{
        if (ent->trace_type == TR_Data)
                bt->data_records++;
        else if (ent->trace_type == TR_Instruction)
                bt->instruction_records++;
        else
                bt->other_records++;
}
#endif   /* TRACE_STATS */


/*****************************************************************/

/*
 * Add your own tracers here.
 */

/*****************************************************************/


/* Return true if the memory operation intersects the data range intervals,
   false otherwise. */
static int
data_range_filter(interval_list_t *ivs, generic_transaction_t *mop)
{
        generic_address_t address[NUM_ADDRESS_TYPES];
        interval_t *iv;
        int i;
        int all_ivs_empty = 1;

        address[PHYSICAL] = mop->physical_address;
        address[VIRTUAL] = mop->logical_address;
        for (i = 0; i < NUM_ADDRESS_TYPES; i++) {
                if (VLEN(ivs[i]) == 0)
                        continue;
                all_ivs_empty = 0; /* we have just seen a nonempty interval
                                      list */
                VFOREACH(ivs[i], iv) {
                        if (address[i] + mop->size - 1 >= iv->start
                            && address[i] <= iv->end) {
                                /* Access intersects data range interval: it
                                   passes the filter. */
                                return 1;
                        }
                }
        }

        /* We get here only by not intersecting any data range interval. This
           is a success if and only if there were no intervals. */
        return all_ivs_empty;
}

static cycles_t
trace_mem_hier_operate(conf_object_t *obj, conf_object_t *space,
                       map_list_t *map, generic_transaction_t *mop)
{
        trace_mem_hier_object_t *tmho = (trace_mem_hier_object_t *)obj;
        base_trace_t *bt = tmho->bt;

        /* If the DSTC line(s) that the memop intersects pass the filter, we
           want to see future transactions. */
        if (data_range_filter(bt->data_stc_interval, mop))
                mop->block_STC = 1;

	/* forward operation on to underlying timing_model */
	return (tmho->timing_model == NULL
                ? 0
                : tmho->timing_iface.operate(tmho->timing_model, space, map, mop));
}

static int
is_duplicate(trace_entry_t *a, trace_entry_t *b)
{
        return (a->arch == b->arch
                && a->trace_type == b->trace_type
                && a->cpu_no == b->cpu_no
                && a->read_or_write == b->read_or_write
                && a->va == b->va
                && a->pa == b->pa);
}


static cycles_t
trace_snoop_operate(conf_object_t *obj, conf_object_t *space,
                    map_list_t *map, generic_transaction_t *mop)
{
        trace_mem_hier_object_t *tmho = (trace_mem_hier_object_t *)obj;
        base_trace_t *bt = tmho->bt;

        if (!SIM_mem_op_is_data(mop) || mop->type == Sim_Trans_Cache
            || !data_range_filter(bt->data_interval, mop))
                goto forward;

        bt->current_entry.trace_type = TR_Data;
        bt->current_entry.read_or_write =
                SIM_mem_op_is_write(mop) ? Sim_RW_Write : Sim_RW_Read;

        bt->current_entry.value.data = 0;
        bt->current_entry.cpu_no = -1;
        if (SIM_mem_op_is_from_cpu(mop)) {
                if (mop->size > 0 && mop->size <= sizeof(uint64))
                        bt->current_entry.value.data
                                = SIM_get_mem_op_value_cpu(mop);
                bt->current_entry.cpu_no = SIM_get_processor_number(mop->ini_ptr);
        }

        bt->current_entry.size          = mop->size;
        bt->current_entry.va            = mop->logical_address;
        bt->current_entry.pa            = mop->physical_address;
        bt->current_entry.atomic        = mop->atomic;

        if (mop->ini_type == Sim_Initiator_CPU_X86) {
                x86_memory_transaction_t *xtrans = (void *)mop;
                bt->current_entry.la            = xtrans->linear_address;
                bt->current_entry.linear_access = xtrans->access_linear;
                bt->current_entry.seg_reg       = xtrans->selector;
                bt->current_entry.access_type   = xtrans->access_type;
                bt->current_entry.memory_type   = xtrans->effective_type;
                bt->current_entry.arch          = TA_x86;
        } else if (mop->ini_type == Sim_Initiator_CPU_IA64) {
                bt->current_entry.arch = TA_ia64;
        } else if (SIM_mem_op_is_from_cpu_arch(mop, Sim_Initiator_CPU_V9)) {
                v9_memory_transaction_t *v9_trans = (void *)mop;
                bt->current_entry.arch = TA_v9;
                bt->current_entry.access_type   = v9_trans->access_type;
        } else {
                bt->current_entry.arch = TA_generic;
        }

        cycles_t t = SIM_cycle_count(mop->ini_ptr);
        bt->current_entry.timestamp = t - bt->last_timestamp;
        bt->last_timestamp = t;

        /* If instruction crosses a page call the trace consumer next time. */
        if (mop->page_cross == 1 && SIM_mem_op_is_instruction(mop))
                goto forward;

        /* Give the memory operation to the trace consumer. */
        if (!bt->filter_duplicates
            || !is_duplicate(&bt->last_entry, &bt->current_entry)) {
                bt->last_entry = bt->current_entry;
                bt->trace_consume(bt, &bt->current_entry);
        }

 forward:
        return (tmho->snoop_device == NULL
                ? 0
                : tmho->snoop_iface.operate(tmho->snoop_device, space, map, mop));
}


/* Determine the architecture implemented by the processor object. */
static trace_arch_t
trace_arch_from_cpu(conf_object_t *cpu)
{
        attr_value_t arch = SIM_get_attribute(cpu, "architecture");
        const char *arch_str = SIM_attr_string(arch);
        trace_arch_t ret;

        if (strncmp(arch_str, "x86", 3) == 0) {
                /* x86 or x86-64. */
                ret = TA_x86;
        } else if (strcmp(arch_str, "ia64") == 0) {
                /* ia64. */
                ret = TA_ia64;
        } else if (strcmp(arch_str, "sparc-v9") == 0) {
                /* SPARC V9 */
                ret = TA_v9;
        } else {
                /* Any other architecture. */
                ret = TA_generic;
        }
        
        SIM_attr_free(&arch);
        return ret;
}


static void
trace_instr_operate(lang_void *data, conf_object_t *cpu,
                    linear_address_t la, logical_address_t va,
                    physical_address_t pa, byte_string_t opcode)
{
        base_trace_t *bt = (base_trace_t *) data;

        bt->current_entry.arch = trace_arch_from_cpu(cpu);
        bt->current_entry.trace_type = TR_Instruction;
        bt->current_entry.cpu_no = SIM_get_processor_number(cpu);
        bt->current_entry.size = opcode.len;
        bt->current_entry.read_or_write = Sim_RW_Read;

        if (bt->current_entry.arch == TA_x86) {
                bt->current_entry.linear_access = 0;
                bt->current_entry.seg_reg = 1; /* cs */
                bt->current_entry.la = la;
                bt->current_entry.memory_type = 0;
        }

        bt->current_entry.va = va;
        bt->current_entry.pa = pa;
        bt->current_entry.atomic = 0;
        bt->current_entry.access_type = 0;
        cycles_t t = SIM_cycle_count(cpu);
        bt->current_entry.timestamp = t - bt->last_timestamp;
        bt->last_timestamp = t;

        memcpy(bt->current_entry.value.text, opcode.str, opcode.len);

        bt->last_entry = bt->current_entry;
        bt->trace_consume(bt, &bt->current_entry);
}


/*************** base class *******************/

/* Call when bt->trace_format or bt->file_name have changed. */
static void
raw_mode_onoff_update(base_trace_t *bt)
{
        /* Make sure we are not writing raw output to the console. */
        if (bt->file_name == NULL)
                bt->trace_format = 0; /* text mode */

        /* Change trace consumer to match trace format. */
        if (bt->trace_format == 0)
                bt->trace_consume = text_tracer;
        else
                bt->trace_consume = raw_tracer;
}

/* Decide if this file name indicates that we should use gz compression (by
   looking for ".gz" suffix). */
static int
is_gz_filename(const char *fname)
{
        int len;

        len = strlen(fname);
        return len > 3 && fname[len - 3] == '.' && fname[len - 2] == 'g'
                && fname[len - 1] == 'z';
}

/* Open or close the output file, as appropriate. Call this whenever file_name
   or trace_enabled has changed. */
static set_error_t
file_onoff_update(base_trace_t *bt)
{
        /* Close regular file, if one is open. */
        if (bt->file != NULL)
                fclose(bt->file);
        bt->file = NULL;

#if defined(HAVE_LIBZ)
        /* Close gz file, if one is open. */
        if (GZ_FILE(bt) != NULL)
                gzclose(GZ_FILE(bt));
        GZ_FILE(bt) = NULL;
#endif

        /* Open file if a file name has been set and the tracer is enabled. */
        if (bt->trace_enabled && bt->file_name != NULL) {
                int file_exists = 0;
                if (is_gz_filename(bt->file_name)) {
#if defined(HAVE_LIBZ)
                        /* We have to open the file twice, since gztell always
                           returns 0 for newly opened files. */
                        FILE *f = os_fopen(bt->file_name, "a");
                        file_exists = f && ftell(f) > 0;
                        if (f)
                                os_fclose(f);
                        GZ_FILE(bt) = gzopen(bt->file_name, "a");
#else
                        bt->file_name = NULL;
                        SIM_attribute_error("gzip compression unavailable");
                        return Sim_Set_Illegal_Value;
#endif
                } else {
                        bt->file = os_fopen(bt->file_name,
                                            bt->trace_format == 0 ? "a" : "ab");
                        file_exists = bt->file && ftell(bt->file) > 0;
                }
                if (GZ_FILE(bt) == NULL && bt->file == NULL) {
                        bt->file_name = NULL;
                        SIM_attribute_error("Cannot open file");
                        return Sim_Set_Illegal_Value;
                }
                if (bt->warn_on_existing_file && file_exists) {
                        bt->warn_on_existing_file = 0;
                        SIM_log_info(1, &bt->log, 0,
                                     "Appending trace to existing file %s",
                                     bt->file_name);
                }
        }

        /* Raw output to console is not allowed. */
        raw_mode_onoff_update(bt);

        return Sim_Set_Ok;
}

/* Register or unregister the execution trace callback for all processors,
   depending on the values of trace_enabled and trace_instructions. */
static void
instruction_trace_onoff_update(base_trace_t *bt)
{
        int i;
        attr_value_t all_objects = SIM_get_all_objects();

        for (i = 0; i < all_objects.u.list.size; i++) {
                conf_object_t *obj = all_objects.u.list.vector[i].u.object;
                exec_trace_interface_t *iface = 
                        SIM_c_get_interface(obj, EXEC_TRACE_INTERFACE);
                if (iface) {
                        if (bt->trace_enabled && bt->trace_instructions)
                                iface->register_tracer(
                                        obj, trace_instr_operate, bt);
                        else
                                iface->unregister_tracer(
                                        obj, trace_instr_operate, bt);
                } 
        }
        SIM_attr_free(&all_objects);
}

/* Called for each exception, just prior to servicing them. */
static void
catch_exception_hook(lang_void *data, conf_object_t *cpu, integer_t exc)
{
        base_trace_t *bt = (base_trace_t *) data;

        bt->current_entry.trace_type = TR_Exception;
        bt->current_entry.value.exception = exc;
        bt->current_entry.cpu_no = SIM_get_processor_number(cpu);
        cycles_t t = SIM_cycle_count(cpu);
        bt->current_entry.timestamp = t - bt->last_timestamp;
        bt->last_timestamp = t;

        /* Call the trace handler. */
        bt->last_entry = bt->current_entry;
        bt->trace_consume(bt, &bt->current_entry);
}

/* Called at Simics exit. */
static void
at_exit_hook(lang_void *data)
{
        base_trace_t *bt = (base_trace_t *) data;

        /* Close any open files. */
        bt->file_name = NULL;
        file_onoff_update(bt);
}

/* Register or unregister the exception callback, depending on the values of
   trace_enabled and trace_exceptions. */
static void
exception_trace_onoff_update(base_trace_t *bt)
{
        obj_hap_func_t f = (obj_hap_func_t) catch_exception_hook;

        if (bt->trace_enabled && bt->trace_exceptions)
                SIM_hap_add_callback("Core_Exception", f, bt);
        else
                SIM_hap_delete_callback("Core_Exception", f, bt);
}

/* Data structure for get_all_mem_spaces function */
struct memspace_list {
	conf_object_t *space;
	conf_object_t *cpu;
	struct memspace_list *next;
};

static void
memspace_list_add(struct memspace_list **headp, conf_object_t *space,
		  conf_object_t *cpu)
{
	struct memspace_list *p;

	/* check if already in list */
	for (p = *headp; p; p = p->next)
		if (p->space == space) return;

	p = MM_ZALLOC(1, struct memspace_list);
	p->next = *headp;
	p->space = space;
	p->cpu = cpu;
	*headp = p;
}

/* Find all memory-spaces connected to any cpu. Returns a linked list. */
static struct memspace_list *
find_memspaces(void)
{
        conf_object_t *cpu;
	struct memspace_list *spaces = NULL;

        attr_value_t ifaces = 
                SIM_make_attr_list(
                        1, 
                        SIM_make_attr_string(PROCESSOR_INFO_INTERFACE));
        attr_value_t queues = VT_get_all_objects_impl(ifaces);
        SIM_attr_free(&ifaces);

        for (int i = 0; i < SIM_attr_list_size(queues); i++) {
                attr_value_t phys_attr, phys_io;
                cpu = SIM_attr_object(SIM_attr_list_item(queues, i));

                phys_attr = SIM_get_attribute(cpu, "physical_memory");
                SIM_clear_exception();

                phys_io = SIM_get_attribute(cpu, "physical_io");
                SIM_clear_exception();

                /* Clocks does not have a physical_memory object. */
                if (phys_attr.kind == Sim_Val_Object)
                        memspace_list_add(&spaces, phys_attr.u.object, cpu);

                /* SPARC has physical-io. */
                if (phys_io.kind == Sim_Val_Object)
			memspace_list_add(&spaces, phys_io.u.object, cpu);
        }
        SIM_attr_free(&queues);

	return spaces;
}

/* Logical xor. True if exactly one of a and b are true, otherwise false. */
#define LXOR(a, b) ((!!(a) ^ !!(b)) != 0)

/* Register or unregister memory transaction snoopers, depending on the values
   of trace_enabled and trace_data. Raise a frontend exception and return a
   suitable error code in case of failure. */
static set_error_t
data_trace_onoff_update(base_trace_t *bt)
{
        struct memspace_list *spaces, *iter;
        attr_value_t attr;
        trace_mem_hier_object_t *tmho;

        /* Assume no error until proven otherwise. */
        const char *err = NULL;
        set_error_t ret = Sim_Set_Ok;

        if (!LXOR(bt->memhier_hook, bt->trace_enabled && bt->trace_data)) {
                /* This means that we are already hooked into the memory
                   hierarchy if and only if we need to be. So, nothing more to
                   do. */
                return ret;
        }

        /* Find all memory-spaces connected to any cpu. */
	spaces = find_memspaces();

        if (!bt->memhier_hook) {
                /* We are not hooked into the memory hierarchy, but we need to
                   be. */

                char buf[128];
                conf_class_t *trace_class;
                int i = 0;

                /* Invalidate any cached memory translations, so that we see
                   all memory operations. */
                SIM_flush_all_caches();

                trace_class = SIM_get_class("trace-mem-hier");

                /* Create mem hiers for every memory-space we will trace on. */
                for (iter = spaces; iter; iter = iter->next) {
                        conf_object_t *space = iter->space;
                        attr_value_t prev_tm, prev_sd;

                        /* Reuse existing object if there is one. */
                        vtsprintf(buf, "trace-mem-hier-%d", i++);
                        tmho = (trace_mem_hier_object_t *) SIM_get_object(buf);
                        if (tmho == NULL) {
                                SIM_clear_exception();
                                /* No, we'll have to create it here. */
                                tmho = (trace_mem_hier_object_t *)
                                        SIM_create_object(trace_class, buf,
                                                        SIM_make_attr_list(0));
                                if (tmho == NULL) {
                                        err = "Cannot create trace object";
                                        ret = Sim_Set_Illegal_Value;
                                        goto finish;
                                }
                        }
                        tmho->bt = bt;
                        tmho->obj.queue = iter->cpu;

			/* Set up forwarding. */
                        tmho->timing_model = NULL;
                        tmho->snoop_device = NULL;
                        memset(&tmho->timing_iface, 0,
                               sizeof(tmho->timing_iface));
                        memset(&tmho->snoop_iface, 0,
                               sizeof(tmho->snoop_iface));
                        prev_tm = SIM_get_attribute(space, "timing_model");
                        if (prev_tm.kind == Sim_Val_Object) {
                                timing_model_interface_t *timing_iface;
                                timing_iface = SIM_c_get_interface(
                                        prev_tm.u.object,
                                        TIMING_MODEL_INTERFACE);
                                if (timing_iface != NULL) {
                                        tmho->timing_model = prev_tm.u.object;
                                        tmho->timing_iface = *timing_iface;
                                }
                        }
                        prev_sd = SIM_get_attribute(space, "snoop_device");
                        if (prev_sd.kind == Sim_Val_Object) {
                                timing_model_interface_t *snoop_iface;
                                snoop_iface = SIM_c_get_interface(
                                        prev_sd.u.object,
                                        SNOOP_MEMORY_INTERFACE);
                                if (snoop_iface != NULL) {
                                        tmho->snoop_device = prev_sd.u.object;
                                        tmho->snoop_iface = *snoop_iface;
                                }
                        }

                        /* May have exceptions after
                           SIM_get_attribute. We don't care. */
                        SIM_clear_exception();

                        /* Plug in new memory hierarchy. */
                        attr = SIM_make_attr_object(&tmho->obj);
                        SIM_set_attribute(space, "snoop_device", &attr);
                        if (SIM_clear_exception() != SimExc_No_Exception) {
                                err = "Could not install snoop device";
                                ret = Sim_Set_Illegal_Value;
                                goto finish;
                        }
                        SIM_set_attribute(space, "timing_model", &attr);
                        if (SIM_clear_exception() != SimExc_No_Exception) {
                                err = "Could not install timing model";
                                ret = Sim_Set_Illegal_Value;
                                goto finish;
                        }
                }
        } else {
                /* We are hooked into the memory hierarchy, but we don't need
                   to be. So splice out the trace_mem_hier_object_t from
                   phys_mem.timing_model and phys_mem.snoop_device. */
                for (iter = spaces; iter; iter = iter->next) {
                        conf_object_t *space = iter->space;

                        attr = SIM_get_attribute(space, "timing_model");
                        tmho = (trace_mem_hier_object_t *) 
                                SIM_attr_object(attr);
                        attr = SIM_make_attr_object(tmho->snoop_device);
                        SIM_set_attribute(space, "snoop_device", &attr);
                        if (SIM_clear_exception() != SimExc_No_Exception) {
                                err = "Could not uninstall snoop device";
                                ret = Sim_Set_Illegal_Value;
                                goto finish;
                        }
                        attr = SIM_make_attr_object(tmho->timing_model);
                        SIM_set_attribute(space, "timing_model", &attr);
                        if (SIM_clear_exception() != SimExc_No_Exception) {
                                err = "Could not uninstall timing model";
                                ret = Sim_Set_Illegal_Value;
                                goto finish;
                        }
                }
        }

        /* If we get here, we changed state. */
        bt->memhier_hook = !bt->memhier_hook;

  finish:
        if (err != NULL)
                SIM_attribute_error(err);

	/* free the linked list */
	for (iter = spaces; iter; ) {
		struct memspace_list *next = iter->next;
		MM_FREE(iter);
		iter = next;
	}
        return ret;
}



static set_error_t
set_raw(void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;

        bt->trace_format = !!val->u.integer;
        raw_mode_onoff_update(bt);
        if (bt->trace_format != !!val->u.integer) {
                /* Change to raw mode was vetoed. */
                SIM_attribute_error("Raw output must be written to a file");
                return Sim_Set_Illegal_Value;
        }
        return Sim_Set_Ok;
}


static attr_value_t
get_raw(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        return SIM_make_attr_integer(bt->trace_format);
}


static set_error_t
set_consumer(void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        trace_consume_interface_t *consume_iface;

        if (val->kind == Sim_Val_Nil) {
                bt->consumer = NULL;
                if (bt->trace_format == 0)
                        bt->trace_consume = text_tracer;
                else
                        bt->trace_consume = raw_tracer;
                return Sim_Set_Ok;
        }

        consume_iface = SIM_c_get_interface(val->u.object, 
                                            TRACE_CONSUME_INTERFACE);
        if (!consume_iface)
                return Sim_Set_Interface_Not_Found;

        bt->consume_iface = *consume_iface;
        bt->consumer = val->u.object;
        bt->trace_consume = external_tracer;
        return Sim_Set_Ok;
}


static attr_value_t
get_consumer(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        return SIM_make_attr_object(bt->consumer);
}


#if defined(TRACE_STATS)
static set_error_t
set_instruction_records(void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        bt->instruction_records = val->u.integer;
        return Sim_Set_Ok;
}


static attr_value_t
get_instruction_records(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        return SIM_make_attr_integer(bt->instruction_records);
}


static set_error_t
set_data_records(void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        bt->data_records = val->u.integer;
        return Sim_Set_Ok;
}


static attr_value_t
get_data_records(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        return SIM_make_attr_integer(bt->data_records);
}


static set_error_t
set_other_records(void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        bt->other_records = val->u.integer;
        return Sim_Set_Ok;
}


static attr_value_t
get_other_records(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        return SIM_make_attr_integer(bt->other_records);
}
#endif   /* TRACE_STATS */

static set_error_t
set_file(void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *) obj;
        set_error_t ret;
        char *old_fn = bt->file_name;

        if (val->kind == Sim_Val_String)
                bt->file_name = MM_STRDUP(val->u.string);
        else
                bt->file_name = NULL;

        /* Try to open/close file. */
        if ((!old_fn && bt->file_name)
            || (old_fn && bt->file_name && strcmp(old_fn, bt->file_name) != 0))
                bt->warn_on_existing_file = 1;
        ret = file_onoff_update(bt);
        if (ret != Sim_Set_Ok) {
                /* Failed, set to no file. */
                if (bt->file_name != NULL)
                        MM_FREE(bt->file_name);
                bt->file_name = NULL;
        }

        MM_FREE(old_fn);
        return ret;
}

static attr_value_t
get_file(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        return SIM_make_attr_string(bt->file_name);
}


static set_error_t
set_enabled(void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        set_error_t ret;

        if (!!bt->trace_enabled == !!val->u.integer)
                return Sim_Set_Ok; /* value did not change */

        /* Change the enabled state and try to start or stop the data
           tracing. */
        bt->trace_enabled = !!val->u.integer;
        ret = data_trace_onoff_update(bt);
        if (ret == Sim_Set_Ok) {
                /* Success, start or stop the other tracers as well. */
                instruction_trace_onoff_update(bt);
                exception_trace_onoff_update(bt);
                file_onoff_update(bt);
        } else {
                /* Failure, revert the change. */
                bt->trace_enabled = !bt->trace_enabled;
        }

        return ret;
}


static attr_value_t
get_enabled(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        return SIM_make_attr_integer(bt->trace_enabled);
}

static set_error_t
set_trace_instructions(void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *) obj;

        /* Update if state changed. */
        if (!!bt->trace_instructions != !!val->u.integer) {
                bt->trace_instructions = !!val->u.integer;
                instruction_trace_onoff_update(bt);
        }
        return Sim_Set_Ok;
}


static attr_value_t
get_trace_instructions(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        return SIM_make_attr_integer(bt->trace_instructions);
}


static set_error_t
set_trace_data(void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *) obj;

        /* Update if state changed. */
        if (!!bt->trace_data != !!val->u.integer) {
                bt->trace_data = !!val->u.integer;
                return data_trace_onoff_update(bt);
        } else {
                return Sim_Set_Ok;
        }
}


static attr_value_t
get_trace_data(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        return SIM_make_attr_integer(bt->trace_data);
}


static set_error_t
set_trace_exceptions(void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *) obj;

        /* Update if state changed. */
        if (!!bt->trace_exceptions != !!val->u.integer) {
                bt->trace_exceptions = !!val->u.integer;
                exception_trace_onoff_update(bt);
        }
        return Sim_Set_Ok;
}


static attr_value_t
get_trace_exceptions(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        return SIM_make_attr_integer(bt->trace_exceptions);
}


static set_error_t
set_filter_duplicates(void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        bt->filter_duplicates = !!val->u.integer;
        return Sim_Set_Ok;
}


static attr_value_t
get_filter_duplicates(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        return SIM_make_attr_integer(bt->filter_duplicates);
}


static set_error_t
set_print_virtual_address(void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        bt->print_virtual_address = !!val->u.integer;
        return Sim_Set_Ok;
}


static attr_value_t
get_print_virtual_address(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        return SIM_make_attr_integer(bt->print_virtual_address);
}


static set_error_t
set_print_physical_address(void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        bt->print_physical_address = !!val->u.integer;
        return Sim_Set_Ok;
}


static attr_value_t
get_print_physical_address(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        return SIM_make_attr_integer(bt->print_physical_address);
}


static set_error_t
set_print_linear_address(void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        bt->print_linear_address = !!val->u.integer;
        return Sim_Set_Ok;
}


static attr_value_t
get_print_linear_address(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        return SIM_make_attr_integer(bt->print_linear_address);
}


static set_error_t
set_print_access_type(void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        bt->print_access_type = !!val->u.integer;
        return Sim_Set_Ok;
}

static attr_value_t
get_print_access_type(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        return SIM_make_attr_integer(bt->print_access_type);
}


static set_error_t
set_print_memory_type(void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        bt->print_memory_type = !!val->u.integer;
        return Sim_Set_Ok;
}


static attr_value_t
get_print_memory_type(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        return SIM_make_attr_integer(bt->print_memory_type);
}


static set_error_t
set_print_data(void *arg, conf_object_t *obj, attr_value_t *val, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        bt->print_data = !!val->u.integer;
        return Sim_Set_Ok;
}


static attr_value_t
get_print_data(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        return SIM_make_attr_integer(bt->print_data);
}


static attr_value_t
get_base_trace(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        trace_mem_hier_object_t *tmho = (trace_mem_hier_object_t *)obj;
        return SIM_make_attr_object(&tmho->bt->log.obj);
}


static attr_value_t
get_timing_model(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        trace_mem_hier_object_t *tmho = (trace_mem_hier_object_t *)obj;
        return SIM_make_attr_object(tmho->timing_model);
}


static attr_value_t
get_snoop_device(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        trace_mem_hier_object_t *tmho = (trace_mem_hier_object_t *)obj;
        return SIM_make_attr_object(tmho->snoop_device);
}


/* Create a new interval_t. Round outwards to the specified number of bits. */
static interval_t
create_interval(generic_address_t start, generic_address_t end, int round)
{
        interval_t iv;
        generic_address_t rmask = ~(generic_address_t)((1 << round) - 1);

        iv.start = MIN(start, end) & rmask;
        iv.end = (MAX(start, end) & rmask) + (1 << round) - 1;
        return iv;
}

static set_error_t
set_data_intervals(void *arg, conf_object_t *obj,
                   attr_value_t *val, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        int address_type = (uintptr_t)arg;
        int i, stc_block;

        /* Find largest DSTC block size. */
        stc_block = 0;
        for (i = 0; i < SIM_number_processors(); i++)
                stc_block = MAX(stc_block, SIM_attr_integer(SIM_get_attribute(
                             SIM_get_processor(i),
                             "memory_profiling_granularity_log2")));
        
        VCLEAR(bt->data_interval[address_type]);
        VCLEAR(bt->data_stc_interval[address_type]);
        for (i = 0; i < val->u.list.size; i++) {
                attr_value_t *as = val->u.list.vector[i].u.list.vector;
                VADD(bt->data_interval[address_type], create_interval(
                             as[0].u.integer, as[1].u.integer, 0));
                VADD(bt->data_stc_interval[address_type], create_interval(
                             as[0].u.integer, as[1].u.integer, stc_block));
        }

        SIM_flush_all_caches();
        return Sim_Set_Ok;
}

static attr_value_t
get_data_intervals(void *arg, conf_object_t *obj, attr_value_t *idx)
{
        base_trace_t *bt = (base_trace_t *)obj;
        int address_type = (uintptr_t)arg;
        attr_value_t ret;

        ret = SIM_alloc_attr_list(VLEN(bt->data_interval[address_type]));
        VFORI(bt->data_interval[address_type], i) {
                interval_t *iv = &VGET(bt->data_interval[address_type], i);
                ret.u.list.vector[i] = SIM_make_attr_list(
                        2, SIM_make_attr_integer(iv->start),
                        SIM_make_attr_integer(iv->end));
        }
        return ret;
}


/* Cache useful information about each processor. */
static void
cache_cpu_info(base_trace_t *bt)
{
        int num, i;

        /* Cache data for each processor. */
        num = SIM_number_processors();
        bt->cpu = MM_MALLOC(num, cpu_cache_t);
        for (i = 0; i < num; i++) {
                bt->cpu[i].cpu = SIM_get_processor(i);

                bt->cpu[i].info_iface =
                        SIM_c_get_interface(bt->cpu[i].cpu,
                                            PROCESSOR_INFO_INTERFACE);

                bt->cpu[i].pa_digits = (
                        bt->cpu[i].info_iface->get_physical_address_width(
                                bt->cpu[i].cpu) + 3) >> 2;
                bt->cpu[i].va_digits = (
                        bt->cpu[i].info_iface->get_logical_address_width(
                                bt->cpu[i].cpu) + 3) >> 2;

                bt->cpu[i].exception_iface =
                        SIM_c_get_interface(bt->cpu[i].cpu, EXCEPTION_INTERFACE);
                vtsprintf(bt->cpu[i].name, "CPU %2d ", i);
        }

        /* Invent reasonable values for non-cpu devices. */
        bt->device_cpu.va_digits = 16;
        bt->device_cpu.pa_digits = 16;
        strcpy(bt->device_cpu.name, "Device ");
}

static conf_object_t *
base_trace_new_instance(parse_object_t *pa)
{
        base_trace_t *bt = MM_ZALLOC(1, base_trace_t);
        obj_hap_func_t f = (obj_hap_func_t) at_exit_hook;
        int i;

        SIM_log_constructor(&bt->log, pa);

        bt->trace_consume = text_tracer; /* text tracing is default */
        bt->trace_instructions = 1;
        bt->trace_data = 1;
        bt->trace_exceptions = 1;
        bt->print_virtual_address = 1;
        bt->print_physical_address = 1;
        bt->print_data = 1;
        bt->print_linear_address = 1;
        bt->print_access_type = 1;
        bt->print_memory_type = 1;
        for (i = 0; i < NUM_ADDRESS_TYPES; i++) {
                VINIT(bt->data_interval[i]);
                VINIT(bt->data_stc_interval[i]);
        }

        cache_cpu_info(bt);

        SIM_hap_add_callback("Core_At_Exit", f, bt);

        return &bt->log.obj;
}


static conf_object_t *
trace_new_instance(parse_object_t *pa)
{
        trace_mem_hier_object_t *tmho = MM_ZALLOC(1, trace_mem_hier_object_t);
        SIM_object_constructor(&tmho->obj, pa);
        return &tmho->obj;
}


void
init_local(void)
{
        class_data_t base_funcs;
        conf_class_t *base_class;
        class_data_t trace_funcs;
        conf_class_t *trace_class;
        timing_model_interface_t *timing_iface, *snoop_iface;

        /* first the base class */
        memset(&base_funcs, 0, sizeof(class_data_t));
        base_funcs.new_instance = base_trace_new_instance;
        base_funcs.description =
                "The base class for the trace mode. "
                " This module provides an easy way of generating traces from Simics. "
                "Actions traced are executed instructions, memory accesses and, "
                "occurred exceptions. Traces will by default be printed as text to the "
                "terminal but can also be directed to a file in which case a binary "
                "format is available as well. It is also possible to control what will "
                "be traced by setting a few of the provided attributes.";
	base_funcs.kind = Sim_Class_Kind_Session;
        
        base_class = SIM_register_class("base-trace-mem-hier", &base_funcs);
        
        SIM_register_typed_attribute(
                base_class, "file",
                get_file, NULL, set_file, NULL,
                Sim_Attr_Session, "s|n", NULL,
                "Name of output file that the trace is written to. If the name"
                " ends in <tt>.gz</tt>, the output will be gzipped.");

        SIM_register_typed_attribute(base_class, "raw-mode",
                                     get_raw, NULL,
                                     set_raw, NULL,
                                     Sim_Attr_Session,
                                     "i", NULL,
                                     "<tt>1</tt>|<tt>0</tt> Set to 1 for raw "
                                     "output format, and 0 for ascii. Raw output "
                                     "format is only supported when writing to "
                                     "a file.");

        SIM_register_typed_attribute(base_class, "consumer",
                                     get_consumer, NULL,
                                     set_consumer, NULL,
                                     Sim_Attr_Session,
                                     "o|n", NULL,
                                     "Optional consumer object. Must implement "
                                     TRACE_CONSUME_INTERFACE ".");

        SIM_register_typed_attribute(base_class, "enabled",
                                     get_enabled, NULL,
                                     set_enabled, NULL,
                                     Sim_Attr_Session,
                                     "i", NULL,
                                     "<tt>1</tt>|<tt>0</tt> Set to 1 to enable "
                                     "tracing, 0 to disable.");

        SIM_register_typed_attribute(base_class, "trace_instructions", 
                                     get_trace_instructions, NULL,
                                     set_trace_instructions, NULL,
                                     Sim_Attr_Session,
                                     "i", NULL,
                                     "<tt>1</tt>|<tt>0</tt> Set to 1 to enable "
                                     "instruction tracing, 0 to disable.");

        SIM_register_typed_attribute(base_class, "trace_data", 
                                     get_trace_data, NULL,
                                     set_trace_data, NULL,
                                     Sim_Attr_Session,
                                     "i", NULL,
                                     "<tt>1</tt>|<tt>0</tt> Set to 1 to enable "
                                     "tracing of data, 0 to disable.");

        SIM_register_typed_attribute(base_class, "trace_exceptions",
                                     get_trace_exceptions, NULL,
                                     set_trace_exceptions, NULL,
                                     Sim_Attr_Session,
                                     "i", NULL,
                                     "<tt>1</tt>|<tt>0</tt> Set to 1 to enable "
                                     "tracing of exceptions, 0 to disable.");

        SIM_register_typed_attribute(base_class, "filter_duplicates",
                                     get_filter_duplicates, NULL,
                                     set_filter_duplicates, NULL,
                                     Sim_Attr_Session,
                                     "i", NULL,
                                     "<tt>1</tt>|<tt>0</tt> Set to 1 to filter "
                                     "out duplicate trace entries. Useful to filter "
                                     "out multiple steps in looping or repeating "
                                     "instructions.");

        SIM_register_typed_attribute(base_class, "print_virtual_address", 
                                     get_print_virtual_address, NULL,
                                     set_print_virtual_address, NULL,
                                     Sim_Attr_Session,
                                     "i", NULL,
                                     "<tt>1</tt>|<tt>0</tt> Set to 1 to enable "
                                     "printing of the virtual address, 0 to disable.");

        SIM_register_typed_attribute(base_class, "print_physical_address", 
                                     get_print_physical_address, NULL,
                                     set_print_physical_address, NULL,
                                     Sim_Attr_Session,
                                     "i", NULL,
                                     "<tt>1</tt>|<tt>0</tt> Set to 1 to enable "
                                     "printing of the physical address, 0 to disable.");

        SIM_register_typed_attribute(base_class, "print_linear_address",
                                     get_print_linear_address, NULL,
                                     set_print_linear_address, NULL,
                                     Sim_Attr_Session,
                                     "i", NULL,
                                     "<tt>1</tt>|<tt>0</tt> Set to 1 to enable "
                                     "printing of the linear address, 0 to disable.");

        SIM_register_typed_attribute(base_class, "print_access_type", 
                                     get_print_access_type, NULL,
                                     set_print_access_type, NULL,
                                     Sim_Attr_Session,
                                     "i", NULL,
                                     "<tt>1</tt>|<tt>0</tt> Set to 1 to enable "
                                     "printing of the memory access type, 0 to disable.");

        SIM_register_typed_attribute(base_class, "print_memory_type", 
                                     get_print_memory_type, NULL,
                                     set_print_memory_type, 0,
                                     Sim_Attr_Session,
                                     "i", NULL,
                                     "<tt>1</tt>|<tt>0</tt> Set to 1 to enable "
                                     "printing of the linear address, 0 to disable.");

        SIM_register_typed_attribute(base_class, "print_data", 
                                     get_print_data, NULL,
                                     set_print_data, NULL,
                                     Sim_Attr_Session,
                                     "i", NULL,
                                     "<tt>1</tt>|<tt>0</tt> Set to 1 to enable "
                                     "printing of data and instruction op codes, "
                                     "0 to disable.");

        SIM_register_typed_attribute(
                base_class, "data_intervals_p",
                get_data_intervals, (void *)(uintptr_t)PHYSICAL,
                set_data_intervals, (void *)(uintptr_t)PHYSICAL,
                Sim_Attr_Session, "[[ii]*]", NULL,
                "List of physical address intervals for data tracing."
                " If no intervals are specified, all addresses are traced.");

        SIM_register_typed_attribute(
                base_class, "data_intervals_v",
                get_data_intervals, (void *)(uintptr_t)VIRTUAL,
                set_data_intervals, (void *)(uintptr_t)VIRTUAL,
                Sim_Attr_Session, "[[ii]*]", NULL,
                "List of virtual address intervals for data tracing."
                " If no intervals are specified, all addresses are traced.");

#if defined(TRACE_STATS)
        SIM_register_typed_attribute(base_class, "instruction_records",
                                     get_instruction_records, NULL,
                                     set_instruction_records, NULL,
                                     Sim_Attr_Session,
                                     "i", NULL,
                                     "Instruction records");

        SIM_register_typed_attribute(base_class, "data_records",
                                     get_data_records, NULL,
                                     set_data_records, NULL,
                                     Sim_Attr_Session,
                                     "i", NULL,
                                     "Data records");

        SIM_register_typed_attribute(base_class, "other_records",
                                     get_other_records, NULL,
                                     set_other_records, NULL,
                                     Sim_Attr_Session,
                                     "i", NULL,
                                     "Other records");
#endif   /* TRACE_STATS */

        /* Register new trace class */
        memset(&trace_funcs, 0, sizeof(class_data_t));
        trace_funcs.new_instance = trace_new_instance;
        trace_funcs.description = 
                "This class is defined in the trace module. It is "
                "used by the tracer to listen to memory traffic on "
                "each CPU.";
        
        trace_class = SIM_register_class("trace-mem-hier", &trace_funcs);

        timing_iface = MM_ZALLOC(1, timing_model_interface_t);
        timing_iface->operate = trace_mem_hier_operate;
        SIM_register_interface(trace_class, "timing_model", timing_iface);
        
        snoop_iface = MM_ZALLOC(1, timing_model_interface_t);
        snoop_iface->operate = trace_snoop_operate;
        SIM_register_interface(trace_class, SNOOP_MEMORY_INTERFACE, snoop_iface);
        
        SIM_register_typed_attribute(trace_class, "base_trace_obj",
                                     get_base_trace, NULL,
                                     NULL, NULL,
                                     Sim_Attr_Session,
                                     "o", NULL,
                                     "Base-trace object (read-only)");

        SIM_register_typed_attribute(trace_class, "timing_model",
                                     get_timing_model, NULL,
                                     NULL, NULL,
                                     Sim_Attr_Session,
                                     "o|n", NULL,
                                     "Timing model (read-only)");

        SIM_register_typed_attribute(trace_class, "snoop_device",
                                     get_snoop_device, NULL,
                                     NULL, NULL,
                                     Sim_Attr_Session,
                                     "o|n", NULL,
                                     "Snoop device (read-only)");
}
