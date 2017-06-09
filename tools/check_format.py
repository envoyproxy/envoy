#!/usr/bin/env python

import re
import os
import os.path
import sys

USAGE = "usage: check_format.py <check|fix> [<directory|file>]"

EXCLUDED_PREFIXES = ("./generated/", "./thirdparty/", "./build", "./.git/",
                     "./bazel-")
SUFFIXES = (".cc", ".h", "BUILD")

CLANG_FORMAT_PATH = os.getenv("CLANG-FORMAT", "clang-format-3.6")
BUILDIFIER_PATH = os.getenv("BUILDIFIER", "/usr/lib/go/bin/buildifier")
ENVOY_BUILD_FIXER_PATH = os.path.join(
    os.path.dirname(os.path.abspath(sys.argv[0])), "envoy_build_fixer.py")
HEADER_ORDER_PATH = os.path.join(
    os.path.dirname(os.path.abspath(sys.argv[0])), "header_order.py")

found_error = False


def printError(error):
  global found_error
  found_error = True
  print "ERROR: %s" % (error)


def checkNamespace(file_path):
  with open(file_path) as f:
    text = f.read()
    if not re.search('^\s*namespace\s+Envoy\s*{', text, re.MULTILINE) and \
       not re.search('NOLINT\(namespace-envoy\)', text, re.MULTILINE):
      printError("Unable to find Envoy namespace or NOLINT(namespace-envoy) for file: %s" % file_path)
      return False
  return True


def checkFilePath(file_path):
  if os.path.basename(file_path) == "BUILD":
    if os.system("%s %s | diff -q %s - > /dev/null" %
                 (ENVOY_BUILD_FIXER_PATH, file_path, file_path)) != 0:
      printError("envoy_build_fixer check failed for file: %s" % (file_path))
    if os.system("cat %s | %s -mode=fix | diff -q %s - > /dev/null" %
                 (file_path, BUILDIFIER_PATH, file_path)) != 0:
      printError("buildifier check failed for file: %s" % (file_path))
    return
  checkNamespace(file_path)
  command = ("%s %s | diff -q %s - > /dev/null" % (HEADER_ORDER_PATH, file_path,
                                                   file_path))
  if os.system(command) != 0:
    printError("header_order.py check failed for file: %s" % (file_path))
  command = ("%s %s | diff -q %s - > /dev/null" % (CLANG_FORMAT_PATH, file_path,
                                                   file_path))
  if os.system(command) != 0:
    printError("clang-format check failed for file: %s" % (file_path))


def fixFilePath(file_path):
  if os.path.basename(file_path) == "BUILD":
    if os.system("%s %s %s" % (ENVOY_BUILD_FIXER_PATH, file_path, file_path)) != 0:
      printError("envoy_build_fixer rewrite failed for file: %s" % (file_path))
    if os.system("%s -mode=fix %s" % (BUILDIFIER_PATH, file_path)) != 0:
      printError("buildifier rewrite failed for file: %s" % (file_path))
    return
  if not checkNamespace(file_path):
    printError("This cannot be automatically corrected. Please fix by hand.")
  command = "%s --rewrite %s" % (HEADER_ORDER_PATH, file_path)
  if os.system(command) != 0:
    printError("header_order.py rewrite error: %s" % (file_path))
  command = "%s -i %s" % (CLANG_FORMAT_PATH, file_path)
  if os.system(command) != 0:
    printError("clang-format rewrite error: %s" % (file_path))


def checkFormat(file_path):
  if file_path.startswith(EXCLUDED_PREFIXES):
    return

  if not file_path.endswith(SUFFIXES):
    return

  if operation_type == "check":
    checkFilePath(file_path)

  if operation_type == "fix":
    fixFilePath(file_path)


def checkFormatVisitor(arg, dir_name, names):
  for file_name in names:
    checkFormat(dir_name + "/" + file_name)


if __name__ == "__main__":
  if len(sys.argv) != 2 and len(sys.argv) != 3:
    print(USAGE)
    sys.exit(1)

  operation_type = sys.argv[1]
  target_path = sys.argv[2] if len(sys.argv) == 3 else "."

  if operation_type not in {"check", "fix"}:
    print(USAGE)
    sys.exit(1)

  if os.path.isfile(target_path):
    checkFormat("./" + target_path)
  else:
    os.chdir(target_path)
    os.path.walk(".", checkFormatVisitor, None)

  if found_error:
    print "ERROR: check format failed. run 'tools/check_format.py fix'"
    sys.exit(1)
