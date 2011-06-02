# 410mods-dynamic-firsttick

from simics import *
from components import *

import cs410_boot_assist

def acked(ignored):
    return 32

def irqswitch(dummy, cpu, status):
    if cpu != conf.cpu0:
        return
    if not status:
        return
    SIM_hap_delete_callback("Core_Interrupt_Status", irqswitch, 0)

    if (not conf.cpu0.iface.x86.has_pending_interrupt() and
        not conf.cpu0.iface.x86.has_waiting_interrupt()):
        conf.cpu0.iface.interrupt_ack.raise_interrupt(conf.cpu0, acked, None)

def booted(cpu, dummy, error):
    SIM_hap_add_callback("Core_Interrupt_Status", irqswitch, 0)

cs410_boot_assist.boot_callbacks.append(booted)
