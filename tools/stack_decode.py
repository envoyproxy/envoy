#!/usr/bin/env python3

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
  # Match something like:
  #     [backtrace] [bazel-out/local-dbg/bin/source/server/_virtual_includes/backtrace_lib/server/backtrace.h:84]
  backtrace_marker = "\[backtrace\] [^\s]+"
  # Match something like:
  #     ${backtrace_marker} #10: SYMBOL [0xADDR]
  # or:
  #     ${backtrace_marker} #10: [0xADDR]
  stackaddr_re = re.compile("%s #\d+:(?: .*)? \[(0x[0-9a-fA-F]+)\]$" % backtrace_marker)
  # Match something like:
  #     #10 0xLOCATION (BINARY+0xADDR)
  asan_re = re.compile(" *#\d+ *0x[0-9a-fA-F]+ *\([^+]*\+(0x[0-9a-fA-F]+)\)")

  try:
    while True:
      line = input_source.readline()
      if line == "":
        return  # EOF
      stackaddr_match = stackaddr_re.search(line)
      if not stackaddr_match:
        stackaddr_match = asan_re.search(line)
      if stackaddr_match:
        address = stackaddr_match.groups()[0]
        file_and_line_number = run_addr2line(object_file, address)
        file_and_line_number = trim_proc_cwd(file_and_line_number)
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
  return subprocess.check_output(["addr2line", "-Cpie", obj_file, addr_to_resolve]).decode('utf-8')


# Because of how bazel compiles, addr2line reports file names that begin with
# "/proc/self/cwd/" and sometimes even "/proc/self/cwd/./". This isn't particularly
# useful information, so trim it out and make a perfectly useful relative path.
def trim_proc_cwd(file_and_line_number):
  trim_regex = r'/proc/self/cwd/(\./)?'
  return re.sub(trim_regex, '', file_and_line_number)


if __name__ == "__main__":
  if len(sys.argv) > 2 and sys.argv[1] == '-s':
    decode_stacktrace_log(sys.argv[2], sys.stdin)
    sys.exit(0)
  elif len(sys.argv) > 1:
    rununder = subprocess.Popen(sys.argv[1:],
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT,
                                universal_newlines=True)
    decode_stacktrace_log(sys.argv[1], rununder.stdout)
    rununder.wait()
    sys.exit(rununder.returncode)  # Pass back test pass/fail result
  else:
    print("Usage (execute subprocess): stack_decode.py executable_file [additional args]")
    print("Usage (read from stdin): stack_decode.py -s executable_file")
    sys.exit(1)
