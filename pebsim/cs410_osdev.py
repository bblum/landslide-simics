from simics import *
from components import *
import cs410_dispatch
import cs410_utils as u

cmdq = []

commands = {
    'nop': 0,
    'tidinfo': 1,   # OBSOLETE.  NWF 201010120
    'switch': 2
}

def switch():
    global commands
    issue_command(commands['switch'], 0, False)

def irqswitch(dummy, cpu, status):
    global cmdq
    cmd = cmdq[0]
    cmdq = cmdq[1:]
    SIM_hap_delete_callback("Core_Interrupt_Status", irqswitch, 0)
    # If we got here, the command was interactive, so break.
    SIM_break_simulation("Command done. Welcome back.")

def osdev_next(dummy, cpu, param):
    global cmdq, irqswitch

    if not cmdq:
        u.copyin(cpu, cpu.ecx, "II", 0, 0)
    else:
        cmd = cmdq[0]
        u.copyin(cpu, cpu.ecx, "II", cmd['op'], cmd['arg'])
        if cmd['interactive']:
            SIM_hap_add_callback("Core_Interrupt_Status", irqswitch, 0)

def acked(ignored):
    return 32 + 6

def issue_command(num, arg, interactive):
    global cmdq
    cmdq.append({ 'op': num,
                  'arg': arg,
                  'interactive': interactive })
    conf.cpu0.iface.interrupt_ack.raise_interrupt(conf.cpu0, acked, None)

# Obsoleted by 410mods-dynamic-tidinfo.py, which is a vastly superior
# mechanism.  NWF 20100120
#
# def cmd_tidinfo(tid):
#    global issue_command
#    issue_command(1, tid, True)
#    SIM_continue(0)
#
#new_command("tidinfo", cmd_tidinfo, args=[arg(int_t, "tid", "?", 1)],
#      short="Ask kernel for thread information by TID",
#      doc="This restarts the simulation long enough to ask for the "
#          + "kernel to translate a TID and dump out what the kernel "
#          + "knew last it saw that thread." )

cs410_dispatch.add_simcall(osdev_next)
