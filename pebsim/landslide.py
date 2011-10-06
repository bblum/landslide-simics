import os

# TODO: factor out this string
cmd_file = "./landslide-save.simics"

SIM_continue(0)
while os.path.isfile(cmd_file):
	SIM_run_command_file(cmd_file, False)
	SIM_continue(0)
