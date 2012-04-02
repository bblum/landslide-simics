# Copyright 2000-2009 Virtutech AB
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

def trace_start_cmd(obj, file, raw, same):
       if not file and raw:
              print "Raw output can only be written to a file."
              return
       try:
              if same:
                     if file or raw:
                            print ("Other arguments are ignored"
                                   + " when -same is specified.")
              else:
                     if file:
                            obj.file = file
                     else:
                            obj.file = None
                     if raw:
                            obj.raw_mode = 1
                     else:
                            obj.raw_mode = 0
              obj.enabled = 1
              if not SIM_get_quiet():
                     print ("Tracing enabled. Writing %s output to %s."
                            % (["text", "raw"][obj.raw_mode],
                               ["'%s'" % obj.file,
                                "standard output"][obj.file == None]))
       except Exception, msg:
              print "Failed to start tracing."
              print "Error message: %s" % msg

new_command("start", trace_start_cmd,
            [arg(filename_t(), "file", "?", ""),
             arg(flag_t, "-raw"),
             arg(flag_t, "-same")],
            type = "base-trace-mem-hier commands",
            alias = "trace-start",
            see_also = ['new-tracer'],
            namespace = "base-trace-mem-hier",
            short = "control default tracer",
            doc = """
An installed tracer is required for this commands to work. Use the
command <b>new-tracer</b> to get a default tracer.

The <b>start</b> command turns on the tracer. The tracer logs all executed
instructions, memory accesses and exceptions. The output is written to
<i>file</i> (appended) if given, otherwise to standard output. If the
<tt>-raw</tt> flag is used, each trace entry is written in binary form; i.e., a
<tt>struct trace_entry</tt>. This structure can be found in trace.h. Raw
format can only be written to file.

If the <tt>-same</tt> flag is used, you will get the same output file
and mode as the last time.

<b>stop</b> switches off the tracer and closes the file. Until you
have given the <b>stop</b> command, you can not be sure that the
entire trace has been written to the file.""")

def trace_stop_cmd(obj):
       obj.enabled = 0
       if not SIM_get_quiet():
              print "Tracing disabled"

new_command("stop", trace_stop_cmd,
            [],
            type = "base-trace-mem-hier commands",
            alias = "trace-stop",
            namespace = "base-trace-mem-hier",
            short = "stop default tracer",
            doc_with = "<base-trace-mem-hier>.start")
