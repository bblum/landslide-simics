from simics import *
from components import *
import cs410_namespace
import cs410_utils

#
# Hash for HAP handlers.  When registering, use long integers with leading
# zeros to get around signedness issues, like this: 0x0abcdef12l
#
hap_handlers = {}

def noop(dummy, cpu, param):
    pass

for k,v in cs410_namespace.magic_opcodes.iteritems():
    hap_handlers[v] = noop

#
# Global hash of breakpoint handler functions
#
breakpoint_handlers = {}

# Global hash of exception handler functions
exn_handlers = {}

#
# Global list of things that want to happen at shutdown
#
halt_callbacks = []

# Context-switch handlers
swproc_handlers = []

#
# Zero indicates that the kernel has not yet told us that it is booted.
#
kernel_up = 0

def __callback_magic(dummy, cpu, param):
    global kernel_up, hap_handlers
    op = cpu.ebx

    # Check what triggered the magic instruction
    #if cpu.cr0 & 0x1 == 1 or cpu.cs[1] == 1:
    #  # Protected mode is engaged
    #  # Or the D bit in CS is turned on, so we're in 32 bit mode
    #  ### We used to differentiate between modes so we would do
    #  ### different reads.  We now always read out 32 bit values.
    #  pass
    #else :
    #  # Protected mode not engaged 
    #  ### We used to read out a 16 bit value here via cpu.bx;
    #  ### instead we'll now just check for the BIOS region and
    #  ### bail in that case.
    #  if cpu.cs[7] == 0x0F0000l :
    #    ## Suppress BIOS warning in early boot.
    #    ## The cycle count here is by observation and might be subject
    #    ## to change.  We may wish to have some boot flag indicate that
    #    ## we have loaded?
    #    if cpu.cycles > 0x01400000l :
    #      print ("Looks like magic from a BIOS region," \
    #              + " bailing out (%x:%x, %x, %x)") \
    #              % ( cpu.cs[7], cpu.eip, str_flag, cpu.cycles)
    #    return

    if kernel_up == 0 and op != cs410_namespace.magic_opcodes["booted"]:
        #print ("Looks like magic preboot," \
        #      + " bailing out (%x:%x, %x, %x)") \
        #      % ( cpu.cs[7], cpu.eip, str_flag, cpu.cycles)
        return

    if hap_handlers.has_key(op) :
        hap_handlers[op](dummy, cpu, param)
    else:
        cs410_utils.log('410-warning', 'Invalid magic %08x %s %08x %08x %08x %04x' %
                        (op, cpu.name, cpu.cycles, cpu.eip, cpu.cr0, cpu.cs[0]))

def __callback_exp(dummy, obj, num):
    if not num in obj.iface.exception.all_exceptions():
        return
    name = obj.iface.exception.get_name(num)
    if exn_handlers.has_key(name):
        exn_handlers[name](obj)

def __callback_breakpoint(arg, obj, brknum, memop) :
  if breakpoint_handlers.has_key( brknum ):
    breakpoint_handlers[brknum] (arg, obj, brknum, memop )
  else :
    SIM_break_simulation("Reached Breakpoint %d" % brknum)

ncr3 = conf.cpu0.iface.int_register.get_number(conf.cpu0, "cr3")
def __callback_creg(dummy, cpu, regnum, val):
    if regnum != ncr3:
        return
    for h in swproc_handlers:
        h(cpu)

SIM_hap_add_callback("Core_Magic_Instruction", __callback_magic, 0)
SIM_hap_add_callback("Core_Control_Register_Write", __callback_creg, 0)
SIM_hap_add_callback("Core_Exception", __callback_exp, 0)
SIM_hap_add_callback("Core_Breakpoint_Memop", __callback_breakpoint, 0)

def add_simcall(fn):
    hap_handlers[cs410_namespace.magic_opcodes[fn.__name__]] = fn

def add_exnhandler(exn, fn):
    if len(exn_handlers) == 0:
        SIM_hap_add_callback("Core_Exception", __callback_exp, 0)
    exn_handlers[exn] = fn

def del_exnhandler(exn, fn):
    del exn_handlers[exn]
    if len(exn_handlers) == 0:
        SIM_hap_delete_callback("Core_Exception", __callback_exp, 0)

def add_swproc_handler(fn):
    swproc_handlers.append(fn)

def del_swproc_handler(fn):
    swproc_handlers.remove(fn)

def run_halt_callbacks():
    for x in halt_callbacks:
        x()
