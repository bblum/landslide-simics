# 410mods-dynamic-nomem-mce: assert a machine-check exception on accesses
# outside RAM

from simics import *
from components import *
import cs410_boot_assist
import cs410_dispatch

nomembp = 0

def acked(ignored):
	return 18

def hbrk(h, b, brknum, m):
	print "Delivering IRQ 18... %08x %08x" % (SIM_get_mem_op_physical_address(m), \
	       SIM_get_mem_op_type(m))
        conf.cpu0.iface.x86.set_pin_status(Pin_Nmi, 1)

def booted(cpu, dummy, error):
	offset_idx = 0
	size_idx = 4
	ranges = []
	# Look at both things which map memeory and figure out
	# all of the ranges that are mapped to something
	mem = conf.phys_mem0.map
	for x in mem:
		lo = x[offset_idx]
		hi = x[size_idx]
		ranges.append((lo, lo + hi))
	mem = conf.pci_mem0.map
	for x in mem:
		lo = x[offset_idx]
		hi = x[size_idx]
		ranges.append((lo, lo + hi))
	# Squash the ranges into the smallest number of ranges
	ranges.sort()
	ranges = mergeRanges(ranges)
	for i in range(0, len(ranges) - 1):
		
		nomembp = SIM_breakpoint(conf.phys_mem0, \
		          Sim_Break_Physical, \
		          Sim_Access_Read | Sim_Access_Write, \
		          ranges[i][1], \
		          ranges[i+1][0] - ranges[i][1], \
		          Sim_Breakpoint_Simulation)
		cs410_dispatch.breakpoint_handlers[nomembp] = hbrk

def mergeRanges(ranges):
	if len(ranges) == 1:
		return [ranges[0]]
	elif ranges[0][1] >= ranges[1][0]:
		newHi = max(ranges[0][1], ranges[1][1])
		newArr = ranges[2:]
		newArr.insert(0, (ranges[0][0], newHi))
		return mergeRanges(newArr)
	else:
		newArr = mergeRanges(ranges[1:])
		newArr.insert(0, ranges[0])
		return newArr

cs410_boot_assist.boot_callbacks.append(booted)
