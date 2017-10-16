#!/usr/bin/env python

import argparse
import re
import os
import os.path
import sys

EXCLUDED_PREFIXES = ("./generated/", "./thirdparty/", "./build", "./.git/",
                     "./bazel-", "./bazel/external")
SUFFIXES = (".cc", ".h", "BUILD")

# Files in these paths can make reference to protobuf stuff directly
GOOGLE_PROTOBUF_WHITELIST = ('ci/prebuilt', 'source/common/protobuf')

CLANG_FORMAT_PATH = os.getenv("CLANG_FORMAT", "clang-format-5.0")
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


# To avoid breaking the Lyft import, we just check for path inclusion here.
def whitelistedForProtobufDeps(file_path):
  return any(path_segment in file_path for path_segment in GOOGLE_PROTOBUF_WHITELIST)

def checkProtobufExternalDepsBuild(file_path):
  if whitelistedForProtobufDeps(file_path):
    return True
  with open(file_path) as f:
    if re.search('"protobuf"', f.read(), re.MULTILINE):
      printError(
          "%s has unexpected direct external dependency on protobuf, use "
          "//source/common/protobuf instead." % file_path)
      return False
    return True


def checkProtobufExternalDeps(file_path):
  if whitelistedForProtobufDeps(file_path):
    return True
  with open(file_path) as f:
    text = f.read()
    if re.search('"google/protobuf', text, re.MULTILINE) or re.search(
        "google::protobuf", text, re.MULTILINE):
      printError(
          "%s has unexpected direct dependency on google.protobuf, use "
          "the definitions in common/protobuf/protobuf.h instead." % file_path)
      return False
    return True


def isBuildFile(file_path):
  basename = os.path.basename(file_path)
  if basename in ["BUILD", "BUILD.bazel"]:
    return True
  if basename.endswith(".BUILD"):
    return True
  return False


def checkFilePath(file_path):
  if isBuildFile(file_path):
    if os.system("%s %s | diff -q %s - > /dev/null" %
                 (ENVOY_BUILD_FIXER_PATH, file_path, file_path)) != 0:
      printError("envoy_build_fixer check failed for file: %s" % file_path)
    if os.system("cat %s | %s -mode=fix | diff -q %s - > /dev/null" %
                 (file_path, BUILDIFIER_PATH, file_path)) != 0:
      printError("buildifier check failed for file: %s" % file_path)
    checkProtobufExternalDepsBuild(file_path)
    return
  checkNamespace(file_path)
  checkProtobufExternalDeps(file_path)
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
      printError("envoy_build_fixer rewrite failed for file: %s" % file_path)
    if os.system("%s -mode=fix %s" % (BUILDIFIER_PATH, file_path)) != 0:
      printError("buildifier rewrite failed for file: %s" % file_path)
    return
  if not checkNamespace(file_path) or not checkProtobufExternalDepsBuild(
      file_path) or not checkProtobufExternalDeps(file_path):
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
  parser = argparse.ArgumentParser(description='Check or fix file format.')
  parser.add_argument('operation_type', type=str, choices=['check', 'fix'],
                      help="specify if the run should 'check' or 'fix' format.")
  parser.add_argument('target_path', type=str, nargs="?", default=".", help="specify the root directory"
                                                                            " for the script to recurse over. Default '.'.")
  parser.add_argument('--add-excluded-prefixes', type=str, nargs="+", help="exclude additional prefixes.")
  args = parser.parse_args()

  operation_type = args.operation_type
  target_path = args.target_path
  if args.add_excluded_prefixes:
    EXCLUDED_PREFIXES += tuple(args.add_excluded_prefixes)

  if os.path.isfile(target_path):
    checkFormat("./" + target_path)
  else:
    os.chdir(target_path)
    os.path.walk(".", checkFormatVisitor, None)

  if found_error:
    print "ERROR: check format failed. run 'tools/check_format.py fix'"
    sys.exit(1)
