#
# 15-410 Simics modifications: XCHG magic
#

from simics import *
from components import *
import cli
import cs410_dispatch
import cs410_osdev
import cs410_utils

atomic_breakpoints = []

def xchg_handle_breakpoints(h,b,brknum,m):
    # print "Atomic assist to the rescue (%d)" % brknum
    cs410_osdev.switch()

# Creates an execute breakpoint that spans userspace.
def create_userspace_breakpoint():
    return SIM_breakpoint(conf.cell0_context, Sim_Break_Virtual,
                          Sim_Access_Execute, 0x1000000, 0xfeffffffL,
                          Sim_Breakpoint_Simulation)

#
# Sets up context-switching on xchg
#
def setup_xchg():
    global atomic_breakpoints

    lockbrk = create_userspace_breakpoint()
    cli.quiet_run_command('set-pattern %d "0xf0" "0xff"' % lockbrk)
    atomic_breakpoints.append(lockbrk)

    for instr in [ "xadd", "xchg", "cmpxchg" ]:
        brk = create_userspace_breakpoint()
        cli.quiet_run_command('set-prefix %d "%s"' % (brk, instr))
        atomic_breakpoints.append(brk)

    for brk in atomic_breakpoints:
        cs410_dispatch.breakpoint_handlers[brk] = xchg_handle_breakpoints

    # print "Atomic assist present %s" % atomic_breakpoints

def enablebps(bp):
    cli.quiet_run_command("enable %d" % bp)

def disablebps(bp):
    cli.quiet_run_command("disable %d" % bp)

# This needs to run "alone" so that it can make all the API calls it
# needs to.
def swat_help(dummy, cpu, param):
    global atomic_breakpoints

    if not atomic_breakpoints:
        setup_xchg()

    f = enablebps
    if cpu.ecx == 0:
        f = disablebps

    for brk in atomic_breakpoints:
        f(brk)

def swat(dummy, cpu, param):
    cs410_utils.alone(swat_help, dummy, cpu, param)

cs410_dispatch.add_simcall(swat)
