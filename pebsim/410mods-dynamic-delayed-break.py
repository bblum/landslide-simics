from simics import *
from cli import *
from sim_commands import classic_break_cmd
import cs410_boot_assist

stif = SIM_get_class_interface('symtable', 'symtable')

booted = 0
babs = [ ]

def fire(cpu, times, error) :
    global booted, babs
    if (booted) :
        print "Not reinstating break-after-boot commands despite second 'boot'"
        return

    booted = 1
    for (s,l,r,w,x) in babs :
        try :
            classic_break_cmd(stif.eval_sym(cpu,s,[],'v'),l,r,w,x)
        except SimExc_General, msg:
            print " !!> Failed to enable breakpoint on '%s' : %s" % (s, msg)
            pass
        except CliError, msg:
            print " !!> Failed to enable breakpoint on '%s' : %s" % (s, msg)
            pass

def bab_cmd(sym, len, r, w, x):
    global booted, babs
    if (booted) :
        print "Calling break-after-boot after boot?  Try break (sym ...) instead."
        return
    else :
        print "Setting deferred break on %r" % sym
        babs.append((sym,len,r,w,x))

new_command("break-after-boot", bab_cmd,
            args=[arg(str_t, "expression"),
             arg(uint64_t, "length", "?", 1),
             arg(flag_t, "-r"), arg(flag_t, "-w"), arg(flag_t, "-x")],
            short="set boot-deferred breakpoint",
            see_also = ["break"],
            doc  = """Set a breakpoint which is symbolically resolved at kernel boot time.
                      Note that calling this after boot will issue a warning and revert to
                      setting a breakpoint.""")

cs410_boot_assist.boot_callbacks.append(fire)
