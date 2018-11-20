#!/usr/bin/env python

# Call addr2line as needed to resolve addresses in a stack trace,
# de-interleaving the log lines from multiple threads if required.
#
# Two ways to call:
#   1) No arguments means this script will treat stdin as log output from
#   Envoy
#   2) Some arguments means run a subprocess with that command line and send
#   stderr through the script
#
# In each case this script will decode any backtrace log lines found and echo
# back all non-Backtrace lines untouched.

import collections
import re
import subprocess
import sys

Backtrace = collections.namedtuple("Backtrace", "log_prefix obj_list")
AddressList = collections.namedtuple("AddressList", "obj_file addresses")


# Process the log output looking for stacktrace snippets, print them out once
# the entire stack trace has been read.  End when EOF received.
def decode_stacktrace_log(input_source):
  traces = {}
  # Match something like [backtrace]
  # bazel-out/local-dbg/bin/source/server/_virtual_includes/backtrace_lib/server/backtrace.h:84]
  backtrace_marker = "\[backtrace\] [^\s]+"
  trace_begin_re = re.compile("^(.+)%s Backtrace thr<(\d+)> obj<(.+)>" % backtrace_marker)
  stackaddr_re = re.compile("%s thr<(\d+)> #\d+ (0x[0-9a-fA-F]+) " % backtrace_marker)
  new_object_re = re.compile("%s thr<(\d+)> obj<(.+)>$" % backtrace_marker)
  trace_end_re = re.compile("%s end backtrace thread (\d+)" % backtrace_marker)

  # build a dictionary indexed by thread_id, value is a Backtrace namedtuple
  try:
    while True:
      line = input_source.readline()
      if line == "":
        return  # EOF
      begin_trace_match = trace_begin_re.search(line)
      if begin_trace_match:
        log_prefix, thread_id, objfile = begin_trace_match.groups()
        traces[thread_id] = Backtrace(log_prefix=log_prefix, obj_list=[])
        traces[thread_id].obj_list.append(AddressList(obj_file=objfile, addresses=[]))
        continue
      stackaddr_match = stackaddr_re.search(line)
      if stackaddr_match:
        thread_id, address = stackaddr_match.groups()
        traces[thread_id].obj_list[-1].addresses.append(address)
        continue
      new_object_match = new_object_re.search(line)
      if new_object_match:
        thread_id, newobj = new_object_match.groups()
        traces[thread_id].obj_list.append(AddressList(obj_file=newobj, addresses=[]))
        continue
      trace_end_match = trace_end_re.search(line)
      if trace_end_match:
        thread_id = trace_end_match.groups()[0]
        output_stacktrace(thread_id, traces[thread_id])
      else:
        # Pass through print all other log lines:
        sys.stdout.write(line)
  except KeyboardInterrupt:
    return


# Execute addr2line with a particular object file and input string of addresses
# to resolve, one per line.
#
# Returns list of result lines
def run_addr2line(obj_file, piped_input):
  addr2line = subprocess.Popen(["addr2line", "-Cpisfe", obj_file],
                               stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE)
  output_stdout, _ = addr2line.communicate(piped_input)
  return output_stdout.split("\n")


# Output one stacktrace after passing it through addr2line with appropriate
# options
def output_stacktrace(thread_id, traceinfo):
  output_lines = []
  for address_list in traceinfo.obj_list:
    piped_input = ""
    obj_name = address_list.obj_file
    for stack_addr in address_list.addresses:
      piped_input += (stack_addr + "\n")
    output_lines += run_addr2line(obj_name, piped_input)

  resolved_stack_frames = enumerate(output_lines, start=1)
  sys.stdout.write(
      "%s Backtrace (most recent call first) from thread %s:\n" % (traceinfo.log_prefix, thread_id))
  for stack_frame in resolved_stack_frames:
    sys.stdout.write("  #%s %s\n" % stack_frame)


if __name__ == "__main__":
  if len(sys.argv) > 1:
    rununder = subprocess.Popen(sys.argv[1:], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    decode_stacktrace_log(rununder.stdout)
    rununder.wait()
    sys.exit(rununder.returncode)  # Pass back test pass/fail result
  else:
    decode_stacktrace_log(sys.stdin)
  sys.exit(0)
