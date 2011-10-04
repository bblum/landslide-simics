# TODO: factor out this string
cmd_file = "./landslide-save.simics"

while True:
	SIM_continue(0)
	SIM_run_command_file(cmd_file, False)
