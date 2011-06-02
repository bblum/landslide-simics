#
# 15-410 Simics mods, Core Magic Handlers
#

from simics import *
from components import *
import cs410_utils
import cs410_dispatch
    
# Breaks the simulation
def breakpoint(dummy, cpu, param):
    SIM_break_simulation("Magic Breakpoint Reached.")

# Prints a string from system memory to the simics console
def lputs(dummy, cpu, param):
    if cpu.cs[0] & 3:
        mc = 'udbg'
    else:
        mc = 'kdbg'
    cs410_utils.log(mc, cs410_utils.copyout_str(cpu, cpu.ecx))

# Loads memory size in kilobytes into %esi
def memsize(dummy, cpu, param):
    cpu.eax = conf.system_cmp0.memory_megs * 1024

# Writes 1 into eax to determine whether we are in Simics.
def in_simics(dummy, cpu, param):
    cpu.eax = 0x15410DE0

def halt(dummy, cpu, param):
    cs410_utils.log('410-core', 'Halted.')
    cs410_dispatch.run_halt_callbacks()
    SIM_quit(0)

# Common code to register the below functions.
for fn in [in_simics, lputs, breakpoint, memsize, halt]:
    cs410_dispatch.add_simcall(fn)
