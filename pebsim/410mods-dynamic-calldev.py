# Simics Call Device
# When a simics call happens, this is the device whose problem it is.

from simics import *
from components import *
import cs410_dispatch

class simcall_dev:
    def __init__(self, obj):
        self.obj = obj
        obj.object_data = self
        self.mapin(0x7f)
    
    def mapin(self, where):
        m = map_info_t()
        m.base = where
        m.start = where
        m.length = 1
        m.function = 0
        m.priority = 0
        m.align_size = 1
        m.reverse_endian = 0
        isab = conf.isa_bus0
        return isab.iface.map_demap.add_map(isab, self.obj, None, m)

def new_instance(parse_obj):
    obj = VT_alloc_log_object(parse_obj)
    simcall_dev(obj)
    return obj

def finalize_instance(obj):
    pass

def simcall_dev_map(obj, mem_or_io, info):
    return 0

def simcall_dev_op(obj, mop, info):
    orig = SIM_get_mem_op_value_le(mop)
    ocpu = SIM_get_object('cpu%d' % orig)
    cs410_dispatch.__callback_magic(None, ocpu, None)
    return Sim_PE_No_Exception

class_data = class_data_t()
class_data.new_instance = new_instance
class_data.finalize_instance = finalize_instance
class_data.description = """Simics call device."""
SIM_register_class("simcall-dev", class_data)

io_if = io_memory_interface_t()
io_if.map = simcall_dev_map
io_if.operation = simcall_dev_op
SIM_register_interface("simcall-dev", "io-memory", io_if)

sd = pre_conf_object('simcall_dev0', 'simcall-dev')
SIM_add_configuration([sd], None)
