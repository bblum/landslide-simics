from simics import *
from components import *
import cs410_dispatch
import cs410_utils as u

tidcontext = {} # int -> frame | cpu
tidtaskname = {} # int -> string
cputid = {} # cpu -> tid oncore | None

def offcore_cb(tid, cpu, dummy) :
  """This callback fires before we take the fault.  Read out the
  CPU state and store it in our map"""

  u.log('410-tidinfo', 'OFFCORE %r %d' % (cpu, tid));

  tidcontext[tid] = u.trap_frame_from_cpu(cpu)
  SIM_hap_delete_callback_obj('Core_Exception', cpu, offcore_cb, tid)
  SIM_hap_add_callback_obj('Core_Exception_Return', cpu, 0, oncore_cb, tid)

def go_oncore(tid,cpu) :
  try :
    tidtaskname[tid] = u.loadedmods['410mods-dynamic-userdebug'] \
                         .user_process_registry[cpu.cr3]
  except KeyError :
    if tid in tidtaskname:
      del tidtaskname[tid]
  tidcontext[tid] = cpu
  SIM_hap_add_callback_obj('Core_Exception', cpu, 0, offcore_cb, tid)

def oncore_cb(tid, cpu, dummy) :
  """This callback happens before IRET mutates state.  Put the CPU structure
  into the map so that we know to ask it directly, below."""
  if u.copyout_int(cpu, cpu.esp+4) & 0x3 != 0x0 :
    u.log('410-tidinfo', 'ONCORE %r %d' % (cpu, tid));
    go_oncore(tid,cpu)
    SIM_hap_delete_callback_obj('Core_Exception_Return', cpu, oncore_cb, tid)

def tidinfo_set(dummy, cpu, param) :
  """Bind a tid to the current CPU as soon as it mode switches down"""
  tid = cpu.ecx

  u.log('410-tidinfo', 'SET %r %d' % (cpu, tid));

  if tid in tidcontext and tidcontext[tid] == cpu.__class__ :
    if tidcontext[tid] == cpu :
      u.log('410-tidinfo', 'WARN: Oncore called twice in a row on %r for tid %d' % (cpu,tid));

    # Purge any existing callbacks to reset our state machine; we'll add
    # the right one below
    SIM_hap_delete_callback_obj('Core_Exception', tidcontext[tid], offcore_cb, tid)
    SIM_hap_delete_callback_obj('Core_Exception_Return', tidcontext[tid], oncore_cb, tid)

    # Remove whichever other tid in whatever state had previously been on this CPU
  if cpu in cputid and cputid[cpu] is not None : 
    SIM_hap_delete_callback_obj('Core_Exception', cpu, offcore_cb, cputid[cpu])
    SIM_hap_delete_callback_obj('Core_Exception_Return', cpu, oncore_cb, cputid[cpu])

  cputid[cpu] = tid

  if cpu.cs[2] != 0 :
    u.log('410-tidinfo', 'WARN: Oncore called from ring %d' % cpu.cs[2]);
    go_oncore(tid, cpu) # Go oncore immediately.  
  else :
    # We go oncore once we engage a mode switch, since the kernel cannot
    # actually tell us when we go back on core
    SIM_hap_add_callback_obj('Core_Exception_Return', cpu, 0, oncore_cb, tid)

def tidinfo_del(dummy, cpu, param) :
  tid = cpu.ecx

  u.log('410-tidinfo', 'DEL %r %d' % (cpu, tid));

  if tid in tidcontext : 
    if tidcontext[tid] == cpu.__class__ :
      u.log('410-tidinfo', \
        'WARN: DEL called from CPU %r for TID %d which is ONCORE on %r' \
        % (cpu,tid,tidcontext[cpu]));
      del cputid[tidcontext[tid]]

  SIM_hap_delete_callback('Core_Exception', offcore_cb, tid)
  SIM_hap_delete_callback('Core_Exception', offcore_cb, tid)
  SIM_hap_delete_callback('Core_Exception_Return', oncore_cb, tid)
  SIM_hap_delete_callback('Core_Exception_Return', oncore_cb, tid)

  if tid in tidcontext:
    del tidcontext[tid]
  if tid in tidtaskname:
    del tidtaskname[tid]

def cmd_tidinfo(tid):
  if not tidcontext:
    print "This kernel does not appear to support the modern infrastructure for this command."
    print "*** If this is a reference kernel, perhaps you need to 'make update'? If that doesn't help, contact course staff."
    return
  if tid not in tidcontext :
    print "Kernel did not register a tid %d; are you sure that's right?" % tid
    return

  if tid in tidtaskname :
    u.log('410', 'TID %d symbols are in %s' % (tid, tidtaskname[tid]))
  else :
    u.log('410', 'TID %d has unknown symbol table' % (tid))

  tf = tidcontext[tid]
  if isinstance(tf, dict) :
    # Off core, stored decoded frame at registration time
    u.log('410', 'TID %d is off-core right now' % tid)
    u.print_trap_frame(tf)
  else :
    # On core, read from cpu right now
    u.log('410', 'TID %d is on-core right now: %s' % (tid, tf.name))
    u.print_trap_frame(u.trap_frame_from_cpu(tf))

def cmd_tids() :
  u.log('410', 'TIDs registered right now: %r' % tidcontext.keys());

cs410_dispatch.add_simcall(tidinfo_set)
cs410_dispatch.add_simcall(tidinfo_del)
new_command("tidinfo", cmd_tidinfo, args=[arg(int_t, "tid")],
      short="Ask kernel for thread information by TID",
      doc="This reports thread information as derived from simulation state")
new_command("tids", cmd_tids,
      short="Report the list of tids registered for tidinfo debugging.",
      doc="This reports the list of tids being tracked.")

# u.logging['410-tidinfo'] = True
# u.printing['410-tidinfo'] = True
u.log('410-tidinfo', 'tidinfo loaded')
