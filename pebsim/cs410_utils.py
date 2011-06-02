"""A collection of utility functions.

Logging utilities, module-loading utilities, and simics interface helpers.
"""

from simics import *
from components import *
import os
import sys
import traceback
import struct

  # A container of the loaded modules.  Mostly handy to give
  # handles to the dynamics.
loadedmods = {}

working_dir = os.environ["OS_PROJ_PATH"]

klog = "kernel.log"
if os.environ.has_key("OS_KERNEL_LOG"):
    klog = os.environ["OS_KERNEL_LOG"]

user_prog_path = working_dir + "/temp/"
user_src_path = working_dir + "/user/progs/"
test_src_path = working_dir + "/410user/progs/"
img_path = working_dir + "/bootfd.img"

log_file = working_dir + "/" + klog

logfd = file(log_file, "w")

logging = { '410-core': False,
            '410-error': True,
            '410-warning': True,
            '410-dyn': False,
            '410-interrupts': True,
            'user-dyn': False,
            'udbg': True,
            'kdbg': True,
            '410': True }

printing = { '410-core': False,
             '410-error': True,
             '410-warning': True,
             '410-dyn': False,
             '410-interrupts': True,
             'user-dyn': False,
             'udbg': True,
             'kdbg': True,
             '410': True }

# Make some sane guesses at the current symbol table's source
kern_paths = [working_dir + "/kernel", working_dir + "/game"]
for kern_path in kern_paths:
    if os.path.isfile(kern_path):
        break

# Copies len bytes out of the system from address addr. Returns a buffer
# containing these bytes.
def copyout(cpu, addr, type):
    """copyout(cpu, addr, type) -> tuple

    Unpacks a structure with type specifier 'type' from virtual address 'addr'
    for cpu 'cpu'. For documentation of the type specifier, see struct.unpack.
    """
    ret = ""
    cpi = cpu.iface.processor_info
    len = struct.calcsize(type)
    for i in range(len):
        pa = cpi.logical_to_physical(cpu, addr + i, X86_Vanilla).address
        ch = SIM_read_byte(conf.phys_mem0, pa)
        ret = ret + chr(ch)
    return struct.unpack(type, ret)

# Copies buf into the system at address addr.
def copyin(cpu, addr, type, *vals):
    """copyin(cpu, addr, type, *vals) -> None

    Packs a structure with type specifier 'type' and values 'vals' to virtual
    address 'addr' for cpu 'cpu'. For documentation of the type specifier, see
    struct.pack.
    """
    cpi = cpu.iface.processor_info
    buf = struct.pack(type, *vals)
    for i in range(len(buf)):
        pa = cpi.logical_to_physical(cpu, addr + i, X86_Vanilla).address
        SIM_write_byte(conf.phys_mem0, pa, ord(buf[i]))

# Copies a string out of system memory.
def copyout_str(cpu, addr):
    """copyout_str(cpu, addr) -> string

    Copies a string of up to 256 characters out of system memory starting at
    address 'addr' for cpu 'cpu'. Note that even if the string is shorter than
    256 bytes, all 256 bytes must be mapped.
    """
    v = copyout(cpu, addr, '256s')[0]
    r = ''
    for i in range(len(v)):
        if ord(v[i]) == 0:
            break
        r += v[i]
    return r

def copyout_int(cpu, addr):
    """copyout_int(cpu, addr) -> int

    Copies a 32-bit little-endian integer out of system memory starting at
    address 'addr' for cpu 'cpu'.
    """
    return copyout(cpu, addr, 'I')[0]

# Logs a message of the given type, if messages of that type are being logged.
def log(type, msg):
    """log(type, msg) -> None

    Logs message 'msg' as type 'type'; if logging of 'type' is enabled, the
    message is logged to the kernel log file, and if printing of 'type' is
    enabled, the message is printed on the simics console.
    """
    if not logfd:
        return
    m = "%u %s: %s" % (conf.cpu0.cycles, type, msg)
    if type in logging and logging[type]:
        logfd.write(m + "\n")
        logfd.flush()
    if type in printing and printing[type]:
        print m

def log_exn(us, type, msg):
    """log_exn(us, type, msg) -> None

    Logs the current exception as a message of type 'type' with prefix message
    'msg'. If 'us' is true, the exception occured in 410 code, and we print a
    message asking the user to contact course staff.
    """
    global logging, printing
    ei = sys.exc_info()
    olt = logging[type]
    opt = printing[type]
    logging[type] = True
    printing[type] = True
    log(type, msg)
    log(type, str(ei[0]))
    log(type, str(ei[1]))
    traceback.print_tb(ei[2])
    if us:
        log(type, "This is probably not your fault. Please contact course staff.")
    logging[type] = olt
    printing[type] = opt

def try_load(src, file):
    """try_load(src, file) -> None

    Try to load file 'file' on behalf of 'src'. Note that this pulls 'file' into
    someone else's namespace (I think into cs410_util's - someone who knows
    Python better than I do, please clarify?), and as such this function is
    currently unused in favor of try_loadm().
    """
    try:
        execfile(file)
        log(src, 'loaded "' + file + '"')
    except:
        log_exn(False, src, 'Failed to load: "%s"' % file)

def try_loadm(src, mod):
    """try_loadm(src, mod) -> None

    Try to load module 'mod' on behalf of 'src'. This pulls 'mod' into its own
    namespace, exactly as if one had done:
        import mod
    In a python file somewhere. Logs the exception that caused module loading to
    fail, if it did.
    """
    try:
        loadedmods[mod] = __import__(mod)
        log(src, 'loaded module "' + mod + '"')
    except:
        log_exn(False, src, 'Failed to load: "%s"' % file)

def callstack(cpu):
    """callstack(cpu) -> list

    Returns the current call stack for cpu 'cpu' by walking up the ebp chain.
    """
    ebp = cpu.ebp
    ceip = cpu.eip
    eip = copyout_int(cpu, ebp + 4)
    eips = [ceip, eip]
    while ebp != 0:
        ebp = copyout_int(cpu, ebp)
        eip = copyout_int(cpu, ebp + 4)
        eips.append(eip)
    return eips

def l2p(cpu, addr):
    """l2p(cpu, addr) -> int

    Returns the physical address corresponding with virtual address 'addr' on
    cpu 'cpu'.
    """
    return cpu.iface.processor_info.logical_to_physical(cpu, addr,
                X86_Vanilla).address

def alone_helper(fa):
    (func, args) = fa
    func(*args)

def alone(func, *args):
    SIM_run_alone(alone_helper, (func, args))

def trap_frame_from_cpu(cpu) :
  """Read out an entire trap frame from the CPU state"""
  res = {}

  regs = [
    # General regs 
    "edi", "esi", "ebp", "esp", "ebx", "edx", "ecx", "eax",
    # Other regs
    "eip", "eflags"
  ];

  for r in regs :
    res[r] = getattr(cpu, r)

    # Segsels
  ssels = [ "ss", "gs", "fs", "es", "ds", "cs" ]
  for r in ssels :
    res[r] = getattr(cpu, r)[0]

  return res;

def print_trap_frame(f) :
    """Print a trapframe in a pleasing way"""

    log('410', "eax %08x ebx %08x ecx %08x edx %08x" \
      % (f['eax'], f['ebx'], f['ecx'], f['edx']))
    log('410', "edi %08x esi %08x epb %08x" \
      % (f['edi'], f['esi'], f['ebp']))
    log('410', "esp %08x  ss     %04x eip %08x  cs     %04x" \
      % (f['esp'], f['ss'], f['eip'], f['cs']))
    log('410', " ds     %04x  es     %04x  fs     %04x  gs     %04x" \
      % (f['ds'], f['es'], f['fs'], f['gs']))
    log('410', "efl %08x" % f['eflags'])

log('410-core', 'cs410_utils loaded')
