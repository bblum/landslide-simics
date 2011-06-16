# Copyright 1998-2009 Virtutech AB
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

import cli

# info command prints static information
def get_info(obj):
    return []

# status command prints dynamic information
def get_status(obj):
    return [("Registers",
             [("Value", obj.value)])]

cli.new_info_command("landslide", get_info)
cli.new_status_command("landslide", get_status)
