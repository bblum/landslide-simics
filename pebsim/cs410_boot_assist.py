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

#    print "Simulation claims to have been booted from '%s'" % str

    match = re.match(r'^\(.*\)/(\w*/)*(?P<file>\w+)(|.gz)$', str);
    if not match is None :
      str = match.group('file')

    str = working_dir + "/" + str

#    print " And I'm guessing this means I should look in '%s'" % str

    if (os.path.isfile(str)):
        # Load all subsequent symbol tables with this source as well
        kern_path = str
# I'm pretty sure this is unnecessary; it's not like the kernel symbol table is
# going to change in flight, and even if it does, it's not like we need to keep
# both of them around. Let's reuse deflsym and avoid the assertion failure we
# get from calling new-symtable at runtime.
#        usersym = "usersym%d" % cs410_dispatch.kernel_up
        usersym = "deflsym"
     
        cli.quiet_run_command("%s.source-path \"%s/;%s;%s\"" %
            (usersym, working_dir, user_src_path, test_src_path))
        cli.quiet_run_command("%s.load-symbols %s" % (usersym, str))
        cli.quiet_run_command("cell0_context.symtable %s" % usersym)
    else:
        print "No such kernel image: '%s'; symbolic debugging won't work." % str
#        print " !!> Cannot find that file; ignoring simulation request."
#        print " !!> This probably means that symbolic debugging will not work."
#        print " !!> Note that we are running loader callbacks anyway!"
#        print " !!> Please contact a TA."
        error = 1

    for x in boot_callbacks:
      x(cpu, cs410_dispatch.kernel_up, error)

cs410_dispatch.add_simcall(booted)
