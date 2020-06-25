#!/usr/bin/env python3

import argparse
import re
import sys


def checkSourceLine(line, reportError):
  None


def printError(error):
  print(f"ERROR: {error}")


def checkFormat(line):
  error_messages = []

  def reportError(message):
      error_messages.append(f"'{line}': {message}")

  checkSourceLine(line, reportError)

  return error_messages
  

def checkErrors(errors):
  if errors:
    for e in errors:
      printError(e)
    return True

  return False


if __name__ == "__main__":
  parser = argparse.ArgumentParser(description="Check line format.")
  parser.add_argument(
      "target_line",
      type=str,
      help="specify the line to check the format of")
  
  args = parser.parse_args()

  target_line = args.target_line

  errors = checkFormat(target_line)

  if checkErrors(errors):
    printError("check format failed.")
    sys.exit(1)

  print("PASS")
