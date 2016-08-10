#!/usr/bin/env python

import os
import os.path
import sys

EXCLUDED_PREFIXES = ("./thirdparty/", "./build", "./.git/")
SUFFIXES = (".cc", ".h")

if len(sys.argv) != 4:
  print("usage: check_format.py <directory|file> <clang_format_path> <check|fix>")
  sys.exit(1)

target_path = sys.argv[1]
clang_format_path = sys.argv[2]
operation_type = sys.argv[3]

found_error = False
def printError(error):
  global found_error
  found_error = True
  print "ERROR: %s" % (error)

def checkFormat(file_path):
  if file_path.startswith(EXCLUDED_PREFIXES):
    return

  if not file_path.endswith(SUFFIXES):
    return

  if operation_type == "check":
    command = ("%s %s | diff -q %s - > /dev/null" % (clang_format_path, file_path, file_path))
    if os.system(command) != 0:
      printError("clang-format check failed for file: %s" % (file_path))

  if operation_type == "fix":
    command = "%s -i %s" % (clang_format_path, file_path)
    if os.system(command) != 0:
      printError("clang-format rewrite error: %s" % (file_path))

def checkFormatVisitor(arg, dir_name, names):
  for file_name in names:
    checkFormat(dir_name + '/' + file_name)

if os.path.isfile(target_path):
  checkFormat("./" + target_path)
else:
  os.chdir(sys.argv[1])
  os.path.walk(".", checkFormatVisitor, None)

if found_error:
  print "ERROR: check format failed. run 'make fix_format'"
  sys.exit(1)
