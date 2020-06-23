#! /usr/bin/env python3

# Tests check_spelling_pedantic.py. Normally run from check_spelling_pedantic.sh.

from __future__ import print_function

from run_command import runCommand
import argparse
import logging
import os
import sys

curr_dir = os.path.dirname(os.path.realpath(__file__))
tools = os.path.dirname(curr_dir)
src = os.path.join(tools, 'testdata', 'spelling')
check_spelling = sys.executable + " " + os.path.join(curr_dir, 'check_spelling_pedantic.py')


# Runs the 'check_spelling_pedanic' operation, on the specified file,
# printing the comamnd run and the status code as well as the stdout,
# and returning all of that to the caller.
def runCheckFormat(operation, filename):
  command = check_spelling + " --test-ignore-exts " + operation + " " + filename
  status, stdout, stderr = runCommand(command)
  return (command, status, stdout + stderr)


def getInputFile(filename):
  return os.path.join(src, filename)


def emitStdoutAsError(stdout):
  logging.error("\n".join(stdout))


def expectError(filename, status, stdout, expected_substrings):
  if status == 0:
    logging.error("%s: Expected %d errors, but succeeded" % (filename, len(expected_substrings)))
    return 1
  errors = 0
  for expected_substring in expected_substrings:
    found = False
    for line in stdout:
      if expected_substring in line:
        found = True
        break
    if not found:
      logging.error("%s: Could not find '%s' in:\n" % (filename, expected_substring))
      emitStdoutAsError(stdout)
      errors += 1

  return errors


def checkFileExpectingErrors(filename, expected_substrings):
  command, status, stdout = runCheckFormat("check", getInputFile(filename))
  return expectError(filename, status, stdout, expected_substrings)


def checkFilePathExpectingOK(filename):
  command, status, stdout = runCheckFormat("check", filename)
  if status != 0:
    logging.error("Expected %s to have no errors; status=%d, output:\n" % (filename, status))
    emitStdoutAsError(stdout)
  return status


def checkFileExpectingOK(filename):
  return checkFilePathExpectingOK(getInputFile(filename))


def runChecks():
  errors = 0

  errors += checkFileExpectingOK("valid")
  errors += checkFileExpectingOK("skip_file")
  errors += checkFileExpectingOK("exclusions")

  errors += checkFileExpectingOK("third_party/something/file.cc")
  errors += checkFileExpectingOK("./third_party/something/file.cc")

  errors += checkFileExpectingErrors("typos",
                                     ["spacific", "reelistic", "Awwful", "combeenations", "woork"])
  errors += checkFileExpectingErrors(
      "skip_blocks", ["speelinga", "speelingb", "speelingc", "speelingd", "speelinge"])
  errors += checkFileExpectingErrors("on_off", ["speelinga", "speelingb"])
  errors += checkFileExpectingErrors("rst_code_block", ["speelinga", "speelingb"])
  errors += checkFileExpectingErrors("word_splitting", ["Speeled", "Korrectly"])

  return errors


if __name__ == "__main__":
  parser = argparse.ArgumentParser(description='tester for check_format.py.')
  parser.add_argument('--log', choices=['INFO', 'WARN', 'ERROR'], default='INFO')
  args = parser.parse_args()
  logging.basicConfig(format='%(message)s', level=args.log)

  errors = runChecks()

  if errors != 0:
    logging.error("%d FAILURES" % errors)
    exit(1)
  logging.warning("PASS")
