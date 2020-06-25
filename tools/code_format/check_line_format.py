#!/usr/bin/env python3

import argparse
import re
import sys

NON_TYPE_ALIAS_ALLOWED_TYPES = {
    "^(?![A-Z].*).*$",
    "^(.*::){,1}(StrictMock<|NiceMock<).*$",
    "^(.*::){,1}(Test|Mock|Fake).*$",
    "^Protobuf.*::.*$",
    "^[A-Z]$",
    "^.*, .*",
    r"^.*\[\]$",
}

USING_TYPE_ALIAS_REGEX = re.compile("using .* = .*;")
SMART_PTR_REGEX = re.compile("std::(unique_ptr|shared_ptr)<(.*?)>(?!;)")
OPTIONAL_REF_REGEX = re.compile("absl::optional<std::reference_wrapper<(.*?)>>(?!;)")
NON_TYPE_ALIAS_ALLOWED_TYPE_REGEX = re.compile(f"({'|'.join(NON_TYPE_ALIAS_ALLOWED_TYPES)})")


def whitelistedForNonTypeAlias(name):
  return NON_TYPE_ALIAS_ALLOWED_TYPE_REGEX.match(name)


def checkSourceLine(line, reportError):
  if not USING_TYPE_ALIAS_REGEX.search(line):
    smart_ptrs = SMART_PTR_REGEX.finditer(line)
    for smart_ptr in smart_ptrs:
      if not whitelistedForNonTypeAlias(smart_ptr.group(2)):
        reportError(f"Use type alias for '{smart_ptr.group(2)}' instead. See STYLE.md")

    optional_refs = OPTIONAL_REF_REGEX.finditer(line)
    for optional_ref in optional_refs:
      if not whitelistedForNonTypeAlias(optional_ref.group(1)):
        reportError(f"Use type alias for '{optional_ref.group(1)}' instead. See STYLE.md")


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
  parser.add_argument("target_line", type=str, help="specify the line to check the format of")

  args = parser.parse_args()

  target_line = args.target_line

  errors = checkFormat(target_line)

  if checkErrors(errors):
    printError("check format failed.")
    sys.exit(1)

  print("PASS")
