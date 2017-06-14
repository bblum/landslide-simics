import os
import cli
from simics import *
from components import *
import cs410_utils
from cs410_utils import working_dir, user_src_path, test_src_path
import cs410_dispatch

boot_callbacks = [ ]

#
# Set symbol information in default symbol table
# This is intended for internal use only; userlevel debugging should
# use the userdebug haps.
#
def booted(dummy, cpu, param):
    cs410_dispatch.kernel_up += 1
    error = 0

    print "%d kernel: up %d" % (cpu.cycles, cs410_dispatch.kernel_up)

    str_addr = cpu.ecx
    try :
      str = cs410_utils.copyout_str(cpu, str_addr)
    except SimExc_General, e :
      print "General Exception while reading for set_default_symtab:"
      print e
      return

    print "Simulation claims to have been booted from '%s'" % str

    match = re.match(r'^\(.*\)/(\w*/)*(?P<file>\w+)(|.gz)$', str);
    if not match is None :
      str = match.group('file')

    str = working_dir + "/" + str

    if (os.path.isfile(str)):
        # Load all subsequent symbol tables with this source as well
        kern_path = str
        usersym = "deflsym"

        sp = ""
        for p in [working_dir + "/", user_src_path, test_src_path]:
            if os.path.exists(p):
                sp += p + ";"

        def to_do_alone(cpu, times, error):
            cli.run_command("%s.source-path \"%s\"" % (usersym, sp[:-1]))
            cli.run_command("%s.load-symbols %s" % (usersym, str))
            cli.run_command("system.cell_context.symtable %s" % usersym)
        if len(sp) > 0:
            boot_callbacks.insert(0, to_do_alone)
    else:
        print "No such kernel image: '%s'; symbolic debugging won't work." % str
        error = 1

    def run_callbacks():
        for x in boot_callbacks:
            x(cpu, cs410_dispatch.kernel_up, error)
    cs410_utils.alone(run_callbacks)

cs410_dispatch.add_simcall(booted)
