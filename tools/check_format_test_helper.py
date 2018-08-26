#!/usr/bin/env python

# Tests check_format.py. This must be run in a context where the clang
# version and settings are compatible with the one in the Envoy
# docker. Normally this is run via check_format_test.sh, which
# executes it in under docker.

import argparse
import os
import shutil
import logging
import subprocess

os.putenv("BUILDIFIER_BIN", "/usr/local/bin/buildifier")

tools = os.path.dirname(os.path.realpath(__file__))
tmp = os.path.join(os.getenv('TEST_TMPDIR', "/tmp"), "check_format_test")
src = os.path.join(tools, 'testdata', 'check_format')
check_format = os.path.join(tools, 'check_format.py')
errors = 0

# Echoes and runs an OS command, returning exit status and the captured
# stdout+stderr as a string array.
def runCommand(command):
  stdout = []
  status = 0
  try:
    out = subprocess.check_output(command, shell=True, stderr=subprocess.STDOUT).strip()
    if out:
      stdout = out.split("\n")
  except subprocess.CalledProcessError as e:
    status = e.returncode
    for line in e.output.splitlines():
      stdout.append(line)
  logging.info("%s" % command)
  return status, stdout

# Runs the 'check_format' operation, on the specified file, printing
# the comamnd run and the status code as well as the stdout, and returning
# all of that to the caller.
def runCheckFormat(operation, filename):
  command = check_format + " " + operation + " " + filename
  status, stdout = runCommand(command)
  return (command, status, stdout)

def getInputFile(filename):
  infile = os.path.join(src, filename)
  shutil.copyfile(infile, filename)
  return filename

# Attempts to fix file, returning a 4-tuple: the command, input file name,
# output filename, captured stdout as an array of lines, and the error status
# code.
def fixFileHelper(filename):
  infile = os.path.join(src, filename)
  shutil.copyfile(infile, filename)
  command, status, stdout = runCheckFormat("fix", getInputFile(filename))
  return (command, infile, filename, status, stdout)

# Attempts to fix a file, returning the status code and the generated output.
# If the fix was successful, the diff is returned as a string-array. If the file
# was not fixable, the error-messages are returned as a string-array.
def fixFileExpectingSuccess(file):
  command, infile, outfile, status, stdout = fixFileHelper(file)
  if status != 0:
    print "FAILED:"
    emitStdoutAsError(stdout)
    return 1
  status, stdout = runCommand('diff ' + outfile + ' ' + infile + '.gold')
  if status != 0:
    print "FAILED:"
    emitStdoutAsError(stdout)
    return 1
  return 0

def fixFileExpectingNoChange(file):
  command, infile, outfile, status, stdout = fixFileHelper(file)
  if status != 0:
    return 1
  status, stdout = runCommand('diff ' + outfile + ' ' + infile)
  if status != 0:
    return 1
  return 0

def emitStdoutAsError(stdout):
  logging.error("\n".join(stdout))

def expectError(status, stdout, expected_substring):
  if status == 0:
    logging.error("Expected failure, but succeeded")
    return 1
  for line in stdout:
    if expected_substring in line:
      return 0
  logging.error("Could not find '%s' in:\n" % expected_substring)
  emitStdoutAsError(stdout)
  return 1

def fixFileExpectingFailure(filename, expected_substring):
  command, infile, outfile, status, stdout = fixFileHelper(filename)
  return expectError(status, stdout, expected_substring)

def checkFileExpectingError(filename, expected_substring):
  command, status, stdout = runCheckFormat("check", getInputFile(filename))
  return expectError(status, stdout, expected_substring)

def checkFileExpectingOK(filename):
  command, status, stdout = runCheckFormat("check", getInputFile(filename))
  if status != 0:
    logging.error("status=%d, output:\n" % status)
    emitStdoutAsError(stdout)
  return 0

if __name__ == "__main__":
  parser = argparse.ArgumentParser(description='tester for check_format.py.')
  parser.add_argument('--log', choices=['INFO', 'WARN', 'ERROR'], default='INFO')
  args = parser.parse_args()
  logging.basicConfig(format='%(message)s', level=args.log)
  errors = 0

  # Now create a temp directory to copy the input files, so we can fix them
  # without actually fixing our testdata. This requires chdiring to the temp
  # directory, so it's annoying to comingle check-tests and fix-tests.
  shutil.rmtree(tmp, True)
  os.makedirs(tmp)
  os.chdir(tmp)
  errors += fixFileExpectingSuccess("over_enthusiastic_spaces.cc")
  errors += fixFileExpectingSuccess("extra_enthusiastic_spaces.cc")
  errors += fixFileExpectingSuccess("angle_bracket_include.cc")
  errors += fixFileExpectingFailure("proto_deps.cc",
                                    "unexpected direct dependency on google.protobuf")
  errors += fixFileExpectingSuccess("proto_style.cc")
  errors += fixFileExpectingSuccess("long_line.cc")
  errors += fixFileExpectingSuccess("header_order.cc")
  errors += fixFileExpectingSuccess("license.BUILD")
  errors += fixFileExpectingSuccess("bad_envoy_build_sys_ref.BUILD")
  errors += fixFileExpectingSuccess("proto_format.proto")
  errors += fixFileExpectingFailure("no_namespace_envoy.cc",
                                    "Unable to find Envoy namespace or NOLINT(namespace-envoy)")
  errors += fixFileExpectingFailure("mutex.cc",
                                    "Don't use <mutex> or <condition_variable*>")
  errors += fixFileExpectingFailure("condition_variable.cc",
                                    "Don't use <mutex> or <condition_variable*>")
  errors += fixFileExpectingFailure("condition_variable_any.cc",
                                    "Don't use <mutex> or <condition_variable*>")
  errors += fixFileExpectingFailure(
      "prod_system_time.cc", "Don't reference real-time sources from production code; use injection")
  errors += fixFileExpectingFailure(
      "prod_monotonic_time.cc", "Don't reference real-time sources from production code; use injection")

  errors += fixFileExpectingNoChange("ok_file.cc")

  errors += checkFileExpectingError("over_enthusiastic_spaces.cc",
                                    "./over_enthusiastic_spaces.cc:3: over-enthusiastic spaces")
  errors += checkFileExpectingError("extra_enthusiastic_spaces.cc",
                                    "./extra_enthusiastic_spaces.cc:3: over-enthusiastic spaces")
  errors += checkFileExpectingError("angle_bracket_include.cc",
                                    "envoy includes should not have angle brackets")
  errors += checkFileExpectingError("no_namespace_envoy.cc",
                                    "Unable to find Envoy namespace or NOLINT(namespace-envoy)")
  errors += checkFileExpectingError("proto_deps.cc",
                                    "unexpected direct dependency on google.protobuf")
  errors += checkFileExpectingError("proto_style.cc", "incorrect protobuf type reference")
  errors += checkFileExpectingError("long_line.cc", "clang-format check failed")
  errors += checkFileExpectingError("header_order.cc", "header_order.py check failed")
  errors += checkFileExpectingError("license.BUILD", "envoy_build_fixer check failed")
  errors += checkFileExpectingError("bad_envoy_build_sys_ref.BUILD",
                                    "Superfluous '@envoy//' prefix")
  errors += checkFileExpectingError("proto_format.proto", "clang-format check failed")
  errors += checkFileExpectingError(
      "prod_system_time.cc", "Don't reference real-time sources from production code; use injection")
  errors += checkFileExpectingError(
      "prod_monotonic_time.cc", "Don't reference real-time sources from production code; use injection")

  errors += checkFileExpectingOK("ok_file.cc")

  errors += fixFileExpectingFailure("proto.BUILD",
                                    "unexpected direct external dependency on protobuf")
  errors += checkFileExpectingError("proto.BUILD",
                                    "unexpected direct external dependency on protobuf")

  if errors != 0:
    logging.error("%d FAILURES" % errors)
    exit(1)
  logging.warn("PASS")
