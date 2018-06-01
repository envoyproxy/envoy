#!/usr/bin/env python

# Tests check_format.py. This must be run under docker via
# check_format_test.py, or you are liable to get the wrong clang, and
# all kinds of bad results.

import os
import shutil
import subprocess

def getenvFallback(envvar, fallback):
  val = os.getenv(envvar)
  if not val:
    val = fallback
  return val

tools = os.path.dirname(os.path.realpath(__file__))
tmp = os.path.join(getenvFallback('TEST_TMPDIR', "/tmp"), "check_format_test")
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
  print("%s" % command)
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
    return 1
  status, stdout = runCommand('diff ' + outfile + ' ' + infile + '.gold')
  if status != 0:
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

def emitStdout(stdout):
  for line in stdout:
    print("    %s" % line)

def expectError(status, stdout, expected_substring):
  if status == 0:
    print("Expected failure, but succeeded")
    return 1
  for line in stdout:
    if expected_substring in line:
      return 0
  print("Could not find '%s' in:\n" % expected_substring)
  emitStdout(stdout)
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
    print("status=%d, output:\n" % status)
    emitStdout(stdout)
  return 0

if __name__ == "__main__":
  errors = 0

  # Now create a temp directory to copy the input files, so we can fix them
  # without actually fixing our testdata. This requires chdiring to the temp
  # directory, so it's annoying to comingle check-tests and fix-tests.
  shutil.rmtree(tmp, True)
  os.makedirs(tmp)
  os.chdir(tmp)
  errors += fixFileExpectingSuccess("over_enthusiastic_spaces.cc")
  errors += fixFileExpectingFailure("no_namespace_envoy.cc",
                                    "Unable to find Envoy namespace or NOLINT(namespace-envoy)")
  errors += fixFileExpectingNoChange("ok_file.cc")

  errors += checkFileExpectingError("over_enthusiastic_spaces.cc",
                                    "./over_enthusiastic_spaces.cc:3: over-enthusiastic spaces")
  errors += checkFileExpectingOK("ok_file.cc")

  if errors != 0:
    print("%d FAILURES" % errors)
    exit(1)
  print("PASS")
  exit(0)
