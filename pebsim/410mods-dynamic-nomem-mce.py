# 410mods-dynamic-nomem-mce: assert a machine-check exception on accesses
# outside RAM

from simics import *
from components import *
import cs410_boot_assist
import cs410_dispatch
import cs410_utils
import cli

sim_version = SIM_version_major()

def acked(ignored):
	print("acking")
	return 18

# This is a gross hack but there doesn't appear to be any other method for
# figuring out what exceptions the user is breaking on, so we parse it out of
# the command output. Must be run in an alone() call.
def break_exception_18_present():
	ret, cmdout = cli.quiet_run_command("break-exception -list",
					output_mode = output_modes.unformatted_text)
	return " 18 " in cmdout

def switcher(cpu):
	cs  = cpu.cs[0]
	ss  = cpu.ss[0]
	eip = cpu.eip
	esp = cpu.esp
	eflags = cpu.eflags
	cr0 = cpu.cr0
	handler = 0

	if break_exception_18_present():
		SIM_break_simulation("Exception breakpoint encountered.")

	if SIM_read_phys_memory(cpu, cpu.idtr_base + 144 + 5, 1) & (1 << 7):
		print "Invoking MCE Handler..."
		handler = (SIM_read_phys_memory(cpu, cpu.idtr_base + 144 + 6, 2) \
			<< 16) | SIM_read_phys_memory(cpu, cpu.idtr_base + 144, 2)
	elif SIM_read_phys_memory(cpu, cpu.idtr_base + 64 + 5, 1) & (1 << 7):
		print "Invoking MCE Handler.....Failed"
		print "Invoking DF Handler......"
		handler = (SIM_read_phys_memory(cpu, cpu.idtr_base + 64 + 6, 2) \
			<< 16) | SIM_read_phys_memory(cpu, cpu.idtr_base + 64, 2)
	else:
		print "Invoking MCE Handler.....Failed"
		print "Invoking DF Handler......Failed"
		if sim_version >= "4.6":
			mem = conf.system.motherboard.northbridge.pci_mem.map
		else:
			mem = conf.pci_mem0.map
		cpu.cr0 = cr0 & 0x7fffffff
		for x in mem:
			dev = x[1].name
			if dev == "system.motherboard.ioapic" or dev == "ioapic0":
				handler = x[0]
				break


	if eip < 0x1000000 and cs == 16 and ss == 24:
		new_esp = esp - 12
		cpu.esp = new_esp
		try:
			SIM_write_phys_memory(cpu, new_esp + 8, eflags, 4)
			SIM_write_phys_memory(cpu, new_esp + 4, 16, 4);
			SIM_write_phys_memory(cpu, new_esp + 0, eip, 4);
		except:
			SIM_break_simulation("Exception writing memory...")
	else:
		tr_base = cpu.tr[7]
		esp0 = SIM_read_phys_memory(cpu, tr_base + 4, 4)
		new_esp = esp0 - 20
		cpu.esp = new_esp
		SIM_write_phys_memory(cpu, new_esp + 16, 43, 4)
		SIM_write_phys_memory(cpu, new_esp + 12, esp, 4)
		SIM_write_phys_memory(cpu, new_esp +  8, eflags, 4)
		SIM_write_phys_memory(cpu, new_esp +  4, 35, 4)
		SIM_write_phys_memory(cpu, new_esp +  0, eip, 4)
		cpu.cs[0] = 16
		cpu.cs[1] = 1
		cpu.cs[2] = 0
		cpu.cs[3] = 1
		cpu.cs[4] = 1
		cpu.cs[5] = 1
		cpu.cs[6] = 11
		cpu.cs[7] = 0
		cpu.cs[8] = 4294967295
		cpu.cs[9] = 1

		cpu.ds[0] = 24
		cpu.ds[1] = 1
		cpu.ds[2] = 3
		cpu.ds[3] = 1
		cpu.ds[4] = 1
		cpu.ds[5] = 1
		cpu.ds[6] = 3
		cpu.ds[7] = 0
		cpu.ds[8] = 4294967295
		cpu.ds[9] = 1

		cpu.cpl = 0

	cpu.eip = handler


#might have to check for esp0 not being valid.
def hbrk(h, b, brknum, m):
	cpu = SIM_current_processor()
	print "Invalid memory access detected:"
	print "eip = 0x%08x, va = 0x%08x, pa = 0x%08x" % \
		(cpu.iface.processor_info.get_program_counter(), \
		SIM_get_mem_op_virtual_address(m), \
		SIM_get_mem_op_physical_address(m))
	print "Delivering Exception 18..."
	cs410_utils.alone(switcher, cpu)
	return

def booted(cpu, dummy, error):
	offset_idx = 0
	size_idx = 4
	ranges = []
	# Look at both things which map memeory and figure out
	# all of the ranges that are mapped to something
	if sim_version >= "4.6":
		mem = conf.system.motherboard.phys_mem.map
	else:
		mem = conf.phys_mem0.map
	for x in mem:
		lo = x[offset_idx]
		hi = x[size_idx]
		ranges.append((lo, lo + hi))
	if sim_version >= "4.6":
		mem = conf.system.motherboard.northbridge.pci_mem.map
	else:
		mem = conf.pci_mem0.map
	for x in mem:
		lo = x[offset_idx]
		hi = x[size_idx]
		ranges.append((lo, lo + hi))
	# Squash the ranges into the smallest number of ranges
	ranges.sort()
	ranges = mergeRanges(ranges)
	for i in range(0, len(ranges) - 1):
		if sim_version >= "4.6":
			nomembp = SIM_breakpoint(conf.system.motherboard.phys_mem, \
					Sim_Break_Physical, \
					Sim_Access_Read | Sim_Access_Write, \
					ranges[i][1], \
					ranges[i+1][0] - ranges[i][1], \
					Sim_Breakpoint_Simulation)
		else:
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
