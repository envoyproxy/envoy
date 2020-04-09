#!/bin/sh
#
# Helper script to run tests under valgrind.  Usage:
#    bazel test --run_under=`pwd`/tools/debugging/run-valgrind.sh ...
#

dir=$(dirname "$0")

# In order to add suppressions, it's helpful to run the tool in a mode
# where it uses the suppressions file we have so far, but also
# generates possible new suppressions in the test output, so you can
# paste them into the suppressions file.
#yes | exec valgrind --gen-suppressions=yes --suppressions="$dir/valgrind-suppressions.txt" "$@"

# Ordinarily, we run with cleaner output.
exec valgrind --gen-suppressions=yes --suppressions="$dir/valgrind-suppressions.txt" "$@"
