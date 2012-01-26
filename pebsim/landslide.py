import os

# TODO: factor out this string
cmd_file = "./landslide-save.simics"

if os.path.isfile(cmd_file):
	os.remove(cmd_file)

SIM_continue(0)
while os.path.isfile(cmd_file):
	SIM_run_command_file(cmd_file, False)
	os.remove(cmd_file)
	SIM_continue(0)
