#
# 15-410 Simics mods file loader 
# Nathaniel Filardo (nwf)
# Mutated beyond recognition by Elly (elly1)

import cli
import glob
import sys
import traceback

# n.b.: The trailing comma is a pythonism meaning "no trailing newline".
print "410: Loading libraries...",

# We really need some components:

try:
    import cs410_utils
except:
    print ">>> Can't load 410 utils."
    print "This is probably not your fault. Please contact a TA."
    raise

cs410_utils.try_loadm('410-core', 'cs410_dispatch')
cs410_utils.try_loadm('410-core', 'cs410_boot_assist')
cs410_utils.try_loadm('410-core', 'cs410_next')
cs410_utils.try_loadm('410-core', 'cs410_core_haps')
cs410_utils.try_loadm('410-core', 'cs410_osdev')
print "Done."

#
# Set up source directories and the boot device.
#

from cs410_utils import working_dir, user_src_path, test_src_path, kern_path, img_path

cli.quiet_run_command("flp0.insert-floppy A " + img_path)
cli.quiet_run_command("system_cmp0.cmos-boot-dev A")
(y, m, d, h, min, s, wd, yd, dst) = time.localtime(time.time())
cli.quiet_run_command("rtc0.set-date-time %d %d %d %d %d %d" % (y, m, d, h, min, s))

# The world has been put in order.  But that might be way too boring.
# So glance around and try to load some other files.

loaded_mods = []

print "410: Staff modules...",
for fname in glob.glob("./410mods-dynamic-*.py"):
    cs410_utils.try_loadm('410-dyn', fname[2:-3])
    # Grab the part after 410mods-dynamic-* and put it in the list.
    loaded_mods.append(fname[18:-3])
print "Done."

# User dynamics? We'd better make sure they're in the module path.
sys.path.append(working_dir)
print "410: User modules...",
for fname in glob.glob(working_dir + "/410mods-dynamic-*.py"):
    cs410_utils.try_loadm('user-dyn', fname.split('/')[-1][:-3])
print "Done."

# Let's load some precompiled dynamics.

for fname in glob.glob("./410mods-dynamic-*.pyc"):
    if fname[18:-3] not in loaded_mods:
        cs410_utils.try_loadm('410-dyn', fname[2:-4])

# log('410', 'Loaded.')
# log('410-core', 'This is EXPERIMENTAL. Do not trust it.')
