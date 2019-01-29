#!/usr/bin/env python

# Call addr2line as needed to resolve addresses in a stack trace. The addresses
# will be replaced if they can be resolved into file and line numbers. The
# executable must include debugging information to get file and line numbers.
#
# Two ways to call:
#   1) Execute binary as a subprocess: stack_decode.py executable_file [args]
#   2) Read log data from stdin: stack_decode.py -s executable_file
#
# In each case this script will add file and line information to any backtrace log
# lines found and echo back all non-Backtrace lines untouched.

import collections
import re
import subprocess
import sys


# Process the log output looking for stacktrace snippets, for each line found to
# contain backtrace output extract the address and call add2line to get the file
# and line information. Output appended to end of original backtrace line. Output
# any nonmatching lines unmodified. End when EOF received.
def decode_stacktrace_log(object_file, input_source):
  traces = {}
  # Match something like [backtrace]
  # bazel-out/local-dbg/bin/source/server/_virtual_includes/backtrace_lib/server/backtrace.h:84]
  backtrace_marker = "\[backtrace\] [^\s]+"
  stackaddr_re = re.compile("%s #\d+: .* \[(0x[0-9a-fA-F]+)\]$" % backtrace_marker)

  try:
    while True:
      line = input_source.readline()
      if line == "":
        return  # EOF
      stackaddr_match = stackaddr_re.search(line)
      if stackaddr_match:
        address = stackaddr_match.groups()[0]
        file_and_line_number = run_addr2line(object_file, address)
        sys.stdout.write("%s %s" % (line.strip(), file_and_line_number))
        continue
      else:
        # Pass through print all other log lines:
        sys.stdout.write(line)
  except KeyboardInterrupt:
    return


# Execute addr2line with a particular object file and input string of addresses
# to resolve, one per line.
#
# Returns list of result lines
def run_addr2line(obj_file, addr_to_resolve):
  addr2line = subprocess.Popen(["addr2line", "-Cpie", obj_file, addr_to_resolve],
                               stdout=subprocess.PIPE)
  output_stdout, _ = addr2line.communicate()
  return output_stdout


if __name__ == "__main__":
  if len(sys.argv) > 2 and sys.argv[1] == '-s':
    decode_stacktrace_log(sys.argv[2], sys.stdin)
  elif len(sys.argv) > 1:
    rununder = subprocess.Popen(sys.argv[1:], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    decode_stacktrace_log(sys.argv[1], rununder.stdout)
    rununder.wait()
    sys.exit(rununder.returncode)  # Pass back test pass/fail result
  else:
    print "Usage (execute subprocess): stack_decode.py executable_file [additional args]"
    print "Usage (read from stdin): stack_decode.py -s executable_file"
  sys.exit(0)
