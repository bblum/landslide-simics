# cs410_next.py - Implement the next410 function.
# I have not looked at this, and I am 90% sure it's obsolete; I fixed the parts
# of it that were obviously broken. -- elly1 U2009

from simics import *
from components import *

def step_stepper(cpu, ((startinst, endinst), (donefunc, doneargs))):
        inst = SIM_read_register(cpu, SIM_get_register_number(cpu, "eip"))
        if (inst <= endinst and inst >= startinst):
		SIM_step_post(cpu, 1, step_stepper, ((startinst, endinst), (donefunc, doneargs)))
        else:
		donefunc(doneargs)

def step_internal(cpu, donefunc, doneargs):
    startinst = inst = SIM_read_register( cpu, SIM_get_register_number( cpu, "eip"))
    startpos = currpos = conf.primary_context.symtable.source_at[inst]
    while (currpos == startpos):
        inst += 1
        currpos = conf.primary_context.symtable.source_at[inst]
    endinst = inst - 1

    SIM_step_post(cpu, 1, step_stepper, ((startinst, endinst), (donefunc, doneargs)))
            
def step():
    step_internal(SIM_current_processor(), SIM_break_simulation, "")
    SIM_continue(0)

def pair_to_args((fun, (arg1, arg2))):
	fun(arg1, arg2)

def next_stepper(cpu, ((funstart, funend), origebp, (donefunc, doneargs))):
    inst = SIM_read_register( cpu, SIM_get_register_number( cpu, "eip"))
    ebp = SIM_read_register( cpu, SIM_get_register_number( cpu, "ebp"))
    if (inst < funstart or inst >= funend) and origebp >= ebp:
	    SIM_step_post(cpu, 1, next_stepper, ((funstart, funend), origebp, (donefunc, doneargs)))
    else:
	    donefunc(doneargs)

def next(n):
    if n == 0:
        return
    cpu = SIM_current_processor()
    startinst = inst = SIM_read_register( cpu, SIM_get_register_number( cpu, "eip"))
    [file, line, function] = conf.primary_context.symtable.source_at[inst]
    [funstart, offset, len] = conf.primary_context.symtable.fun_addr[[function, file]]
    funend = funstart + len
    origebp = SIM_read_register( cpu, SIM_get_register_number( cpu, "ebp"))

    # step one line, then pass off to next_stepper
    step_internal(cpu, pair_to_args, (next_stepper, (cpu, ((funstart, funend), origebp, (SIM_break_simulation, "")))))

    SIM_continue(0)    
    next(n-1)

new_command("next410", next, args=[arg(int_t, "n", "?", 1)], short="Step N source lines",
            doc="<b>next</b> Attempts to step <i>n</i> source lines, skipping over function calls.  <i>n</i> defaults to one.\n\
	    \n\
	    In each next, one source line is stepped over, then instructions are stepped through until the program counter returns to within the bounds of the starting function in the same frame, or a higher frame is reached.  Note that there is at least one relatively common situation in which unexpected behavior might occur.") 

returninsts = [0xc3, # near ret
	       0xcb, # far ret
	       0xc2, # near ret with pop
	       0xca, # far ret with pop
	       0xcf, # iret
	      ] 

def break_obj(obj, msg):
	SIM_break_simulation(msg)

def ret_stepper(cpu, (function, funstart, funend)):
	inst = SIM_read_register( cpu, SIM_get_register_number( cpu, "eip"))
	if (inst >= funstart) and (inst < funend) and SIM_read_byte(cpu.physical_memory, SIM_logical_to_physical(cpu, Sim_DI_Instruction, inst)) in returninsts:
		retval = SIM_read_register( cpu, SIM_get_register_number( cpu, "eax"))
		SIM_step_post(cpu, 1, break_obj, "%s returned with 0x%x" % (function, retval))
	else:
		SIM_step_post(cpu, 1, ret_stepper, (function, funstart, funend))

def ret():
	cpu = SIM_current_processor()
	inst = SIM_read_register( cpu, SIM_get_register_number( cpu, "eip"))
	[file, line, function] = conf.primary_context.symtable.source_at[inst]
	[funstart, offset, len] = conf.primary_context.symtable.fun_addr[[function, file]]
	funend = funstart + len

	SIM_step_post( cpu, 1, ret_stepper, (function, funstart, funend))
	SIM_continue(0)
	
new_command("return", ret, short="Return from a function",
            doc="<b>return</b> steps until the current conventionally structured C or assembly function returns, and attempts to print the return value.")

