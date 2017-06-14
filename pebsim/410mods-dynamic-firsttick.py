# 410mods-dynamic-firsttick

from simics import *
from components import *

import cs410_boot_assist
import cs410_utils

hap_id = None

def acked(ignored):
    return 32

def irqswitch(dummy, cpu, status):
    if cpu != conf.cpu0:
        return
    if not status:
        return
    SIM_hap_delete_callback_id("Core_Interrupt_Status", hap_id)

    if (not conf.cpu0.iface.x86.has_pending_interrupt() and
        not conf.cpu0.iface.x86.has_waiting_interrupt()):
        conf.cpu0.iface.interrupt_ack.raise_interrupt(acked, None)

def booted(cpu, dummy, error):
    global hap_id
    hap_id = SIM_hap_add_callback_obj("Core_Interrupt_Status", conf.cpu0, 0, irqswitch, 0)

cs410_boot_assist.boot_callbacks.append(booted)
