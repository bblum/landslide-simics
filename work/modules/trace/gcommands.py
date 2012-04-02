# Copyright 2004-2009 Virtutech AB
# 
# The contents herein are Source Code which are a subset of Licensed
# Software pursuant to the terms of the Virtutech Simics Software
# License Agreement (the "Agreement"), and are being distributed under
# the Agreement.  You should have received a copy of the Agreement with
# this Licensed Software; if not, please contact Virtutech for a copy
# of the Agreement prior to using this Licensed Software.
# 
# By using this Source Code, you agree to be bound by all of the terms
# of the Agreement, and use of this Source Code is subject to the terms
# the Agreement.
# 
# This Source Code and any derivatives thereof are provided on an "as
# is" basis.  Virtutech makes no warranties with respect to the Source
# Code or any derivatives thereof and disclaims all implied warranties,
# including, without limitation, warranties of merchantability and
# fitness for a particular purpose and non-infringement.

from cli import *
from simics import *

def new_trace_cmd():
       trace_name = "trace0"
       try:
              obj = SIM_get_object(trace_name)
              raise CliError, "Tracer '%s' already exists." % trace_name
       except:
              pass
       try:
              obj = SIM_create_object("base-trace-mem-hier", trace_name, [])
              def message():
                     return ("Trace object '%s' created. Enable tracing with '%s.start'."
                             % (trace_name, trace_name))
              return command_return(value = obj, message = message)
       except:
              raise CliError, "Failed creating trace object '%s'." % trace_name
       
new_command("new-tracer", new_trace_cmd,
            [],
            type = ["Tracing"],
            see_also = ['<base-trace-mem-hier>.start'],
            short = "create a new tracer",
            doc = """
Create a new tracer that connects to each CPU's memory space and
traces instruction and data accesses.
""")
