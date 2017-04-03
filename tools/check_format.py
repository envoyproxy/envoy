#!/usr/bin/env python

import os
import os.path
import sys

EXCLUDED_PREFIXES = ("./thirdparty/", "./build", "./.git/")
SUFFIXES = (".cc", ".h", "BUILD")

if len(sys.argv) != 5:
  print("usage: check_format.py <directory|file> <clang_format_path> <buildifier_path> <check|fix>")
  sys.exit(1)

target_path = sys.argv[1]
clang_format_path = sys.argv[2]
buildifier_path = sys.argv[3]
operation_type = sys.argv[4]

found_error = False
def printError(error):
  global found_error
  found_error = True
  print "ERROR: %s" % (error)

def checkFilePath(file_path):
  if os.path.basename(file_path) == 'BUILD':
    if os.system("cat %s | %s -mode=fix | diff -q %s - > /dev/null" %
                 (file_path, buildifier_path, file_path)) != 0:
      printError("buildifier check failed for file: %s" % (file_path))
    return
  command = ("%s %s | diff -q %s - > /dev/null" % (clang_format_path, file_path, file_path))
  if os.system(command) != 0:
    printError("clang-format check failed for file: %s" % (file_path))

def fixFilePath(file_path):
  if os.path.basename(file_path) == 'BUILD':
    if os.system("%s -mode=fix %s" % (buildifier_path, file_path)) != 0:
      printError("buildifier rewrite failed for file: %s" % (file_path))
    return
  command = "%s -i %s" % (clang_format_path, file_path)
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
    checkFormat(dir_name + '/' + file_name)

if os.path.isfile(target_path):
  checkFormat("./" + target_path)
else:
  os.chdir(sys.argv[1])
  os.path.walk(".", checkFormatVisitor, None)

if found_error:
  print "ERROR: check format failed. run 'make fix_format'"
  sys.exit(1)
