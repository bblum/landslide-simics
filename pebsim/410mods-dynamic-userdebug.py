from simics import *
from components import *
import cli
import cs410_dispatch
from cs410_utils import copyout_str, kern_path, user_prog_path, working_dir
from cs410_utils import user_src_path, test_src_path
import cs410_utils

ncr3 = conf.cpu0.iface.int_register.get_number(conf.cpu0, "cr3")

#
# Hash for user process debugging.
#
user_process_registry = {}

# This is a callback that is run by simics in 'single-threaded' mode (see the
# call to cs410_utils alone below). We cannot invoke new-symtable in
# multithreaded  ('execution') mode, and we don't want other people
# using the partially-populated symtable, so we synchronize globally
# when doing this.
def make_new_symtable(sname, fname, cr3):
    fpath = user_prog_path+fname

    if not os.path.isfile(fpath) or '/' in fname:
        cs410_utils.log('410-warning',
                        'Unable to load user symbols for "%s"' % fname)
        del user_process_registry[cr3]
        return
    
    try:
        cli.quiet_run_command('new-symtable %s' % sname)
        cli.quiet_run_command('%s.load-symbols "%s"' % (sname, kern_path))

        cli.quiet_run_command('%s.load-symbols "%s"' % (sname, fpath))
        cli.quiet_run_command('%s.source-path "%s/;%s/;%s/"' %
            (sname, working_dir, user_src_path, test_src_path))
    except:
        cs410_utils.log('410-warning',
                        'Unable to load user symbols for "%s"' % fname)
        del user_process_registry[cr3]

#
# Registers a user process for debugging
#
def reg_process(dummy, cpu, param):
    fname = ""
    try :
        fname = copyout_str(cpu, cpu.edx)
    except :
        cs410_utils.log('410-warning', '410mods-userdebug.py: Unable to read string at %x; not registering process' % cpu.edx)
        return

    if fname == "" or '/' in fname:
        return

    cr3 = cpu.ecx
    sname = "prog-"+fname
    user_process_registry[cr3] = sname

    # see if symbol table for fname exists.
    # if not make it and load the symbols.
    try:
        SIM_get_object(sname)

        ### FIXME We would like to empty the symbol table if
        ### it exists, but we'll do the best we can by reloading
        ### symbols below.
    except SimExc_General:
        cs410_utils.alone(make_new_symtable, sname, fname, cr3)

    cs410_utils.alone(switch_current_symtable, cpu)

# Unregisters a user process
def unreg_process(dummy, cpu, param):
    # unregister user process
    global user_process_registry

    cr3 = cpu.ecx
    if user_process_registry.has_key(cr3):
        s = user_process_registry[cr3]
        del user_process_registry[cr3]
        cs410_utils.alone(switch_current_symtable, cpu)
        #st = SIM_get_object(s) # This would be the right thing to do but it
        #SIM_delete_object(st)  # doesn't appear to be supported by simics yet

# Registers a fork()ed process for debugging
def reg_child(dummy, cpu, param):
    child_cr3 = cpu.ecx
    parent_cr3 = cpu.edx
    user_process_registry[child_cr3] = user_process_registry[parent_cr3]
    cs410_utils.alone(switch_current_symtable, cpu)

# Sets the current symtable to be the one associated with the cr3
def switch_symtable(cr3):
    if user_process_registry.has_key(cr3):
        cli.quiet_run_command("cell0_context.symtable %s" %
            user_process_registry[cr3])
    else:
        cli.quiet_run_command("cell0_context.symtable deflsym")

# Switch to the symtable associated with the current execution state
def switch_current_symtable(cpu):
    switch_symtable(cpu.iface.int_register.read(cpu, ncr3))

def switch_creg(dummy, cpu, regnum, val):
    if regnum == ncr3:
        switch_symtable(val)

for fn in [reg_process, unreg_process, reg_child]:
    cs410_dispatch.add_simcall(fn)

SIM_hap_add_callback("Core_Control_Register_Write", switch_creg, 0)
