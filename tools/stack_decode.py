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

import re
import sys
import subprocess


# Process the log output looking for stacktrace snippets, print them out once
# the entire stack trace has been read.  End when EOF received.
def decode_stacktrace_log(input_source):
  traces = {}
  trace_begin_re = re.compile('^(.+)\[backtrace\] Backtrace obj<(.+)> thr<(\d+)')
  stackaddr_re = re.compile('\[backtrace\] thr<(\d+)> #(\d+) (0x[0-9a-fA-F]+)$')
  trace_end_re = re.compile('\[backtrace\] end backtrace thread (\d+)')

  # build a dictionary indexed by thread_id:
  #   value is a tuple (log_prefix, obj_file, [list of addresses])
  while True:
    line = input_source.readline()
    if line == '':
      return # EOF
    begin_trace_match = trace_begin_re.search(line)
    if begin_trace_match:
      (log_prefix, objfile, thread_id) = begin_trace_match.groups()
      traces[thread_id] = (log_prefix, objfile, [])
      continue
    match = stackaddr_re.search(line)
    if match:
      (thread_id, frame_idx, address) = match.groups()
      traces[thread_id][2].append((frame_idx, address))
      continue
    trace_end_match = trace_end_re.search(line)
    if trace_end_match:
      thread_id = trace_end_match.groups()[0]
      output_stacktrace(thread_id, traces[thread_id])
    else:
      print line, # Pass through print all other log lines

# Output one stacktrace after passing it through addr2line with appropriate
# options
def output_stacktrace(thread_id, traceinfo):
  (log_prefix, object_file, stack) = traceinfo
  piped_input = ""
  for stack_frame in stack:
    piped_input += (stack_frame[1] + '\n')
  addr2line = subprocess.Popen(['addr2line', '-Cpisfe', object_file], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
  (output_stdout, output_stderr) = addr2line.communicate(piped_input)
  output_lines = output_stdout.split('\n')

  resolved_stack_frames = zip(range(1, len(output_lines)), output_lines)
  print "%s Backtrace (most recent call first) from thread %s:" % (log_prefix, thread_id)
  for stack_frame in resolved_stack_frames:
    print "  #%s %s" % (stack_frame[0], stack_frame[1])

if __name__ == '__main__':
  if len(sys.argv) > 1:
    print sys.argv[1:]
    rununder = subprocess.Popen(sys.argv[1:], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    decode_stacktrace_log(rununder.stderr)
  else:
    decode_stacktrace_log(sys.stdin)
  sys.exit(0)
