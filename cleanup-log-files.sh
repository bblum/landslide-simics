#!/bin/bash

echo "This is a quick script I found useful for cleaning up stale log files en masse."
echo "By default it won't do anything since you might run it by accident and want to keep your log files,"
echo "but feel free to delete this check to turn it on."
exit 1 # <-- delete this line right here

rm -f id/ls-output*log*
rm -f id/ls-setup*log*
rm -f id/ls-id-log*log*
rm -f pebsim/config-id.*
rm -f landslide-trace-*
rm -f ls-id-options*
