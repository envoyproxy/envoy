#!/usr/bin/env python

import argparse
import fileinput
import multiprocessing
import os
import os.path
import re
import subprocess
import sys
import traceback

EXCLUDED_PREFIXES = ("./generated/", "./thirdparty/", "./build", "./.git/",
                     "./bazel-", "./bazel/external", "./.cache")
SUFFIXES = (".cc", ".h", "BUILD", ".md", ".rst")
DOCS_SUFFIX = (".md", ".rst")

# Files in these paths can make reference to protobuf stuff directly
GOOGLE_PROTOBUF_WHITELIST = ('ci/prebuilt', 'source/common/protobuf')

CLANG_FORMAT_PATH = os.getenv("CLANG_FORMAT", "clang-format-5.0")
BUILDIFIER_PATH = os.getenv("BUILDIFIER_BIN", "$GOPATH/bin/buildifier")
ENVOY_BUILD_FIXER_PATH = os.path.join(
    os.path.dirname(os.path.abspath(sys.argv[0])), "envoy_build_fixer.py")
HEADER_ORDER_PATH = os.path.join(
    os.path.dirname(os.path.abspath(sys.argv[0])), "header_order.py")


def checkNamespace(file_path):
  with open(file_path) as f:
    text = f.read()
    if not re.search('^\s*namespace\s+Envoy\s*{', text, re.MULTILINE) and \
       not 'NOLINT(namespace-envoy)' in text:
      return ["Unable to find Envoy namespace or NOLINT(namespace-envoy) for file: %s" % file_path]
  return []


# To avoid breaking the Lyft import, we just check for path inclusion here.
def whitelistedForProtobufDeps(file_path):
  return any(path_segment in file_path for path_segment in GOOGLE_PROTOBUF_WHITELIST)

def findSubstringAndReturnError(pattern, file_path, error_message):
  with open(file_path) as f:
    text = f.read()
    if pattern in text:
      error_messages = [error_message]
      for i, line in enumerate(text.splitlines()):
        if pattern in line:
          error_messages.append("  %s:%s" % (file_path, i + 1))
      return error_messages
    return []

def checkProtobufExternalDepsBuild(file_path):
  if whitelistedForProtobufDeps(file_path):
    return []
  message = ("%s has unexpected direct external dependency on protobuf, use "
    "//source/common/protobuf instead." % file_path)
  return findSubstringAndReturnError('"protobuf"', file_path, message)


def checkProtobufExternalDeps(file_path):
  if whitelistedForProtobufDeps(file_path):
    return []
  with open(file_path) as f:
    text = f.read()
    if '"google/protobuf' in text or "google::protobuf" in text:
      return [
          "%s has unexpected direct dependency on google.protobuf, use "
          "the definitions in common/protobuf/protobuf.h instead." % file_path]
    return []


def isBuildFile(file_path):
  basename = os.path.basename(file_path)
  if basename in {"BUILD", "BUILD.bazel"} or basename.endswith(".BUILD"):
    return True
  return False


def checkFileContents(file_path):
  message = "%s has over-enthusiastic spaces:" % file_path
  return findSubstringAndReturnError('.  ', file_path, message)


def fixFileContents(file_path):
  for line in fileinput.input(file_path, inplace=True):
    # Strip double space after '.'  This may prove overenthusiastic and need to
    # be restricted to comments and metadata files but works for now.
    sys.stdout.write("%s" % (line.replace('.  ', '. ')))


def checkFilePath(file_path):
  error_messages = []
  if isBuildFile(file_path):
    command = "%s %s | diff %s -" % (ENVOY_BUILD_FIXER_PATH, file_path, file_path)
    error_messages += executeCommand(command, "envoy_build_fixer check failed", file_path)

    command = "cat %s | %s -mode=fix | diff %s -" % (file_path, BUILDIFIER_PATH, file_path)
    error_messages += executeCommand(command, "buildifier check failed", file_path)

    error_messages += checkProtobufExternalDepsBuild(file_path)
    return error_messages

  error_messages += checkFileContents(file_path)

  if file_path.endswith(DOCS_SUFFIX):
    return error_messages
  error_messages += checkNamespace(file_path)
  error_messages += checkProtobufExternalDeps(file_path)

  command = ("%s %s | diff %s -" % (HEADER_ORDER_PATH, file_path, file_path))
  error_messages += executeCommand(command, "header_order.py check failed", file_path)

  command = ("%s %s | diff %s -" % (CLANG_FORMAT_PATH, file_path, file_path))
  error_messages += executeCommand(command, "clang-format check failed", file_path)

  return error_messages


# Example target outputs are:
#   - "26,27c26"
#   - "12,13d13"
#   - "7a8,9"
def executeCommand(command, error_message, file_path,
        regex=re.compile(r"^(\d+)[a|c|d]?\d*(?:,\d+[a|c|d]?\d*)?$")):
  try:
    output = subprocess.check_output(command, shell=True, stderr=subprocess.STDOUT).strip()
    if output:
      return output.split("\n")
    return []
  except subprocess.CalledProcessError as e:
      if (e.returncode != 0 and e.returncode != 1):
        return ["ERROR: something went wrong while executing: %s" % e.cmd]
      # In case we can't find any line numbers, record an error message first.
      error_messages = ["%s for file: %s" % (error_message, file_path)]
      for line in e.output.splitlines():
        for num in regex.findall(line):
          error_messages.append("  %s:%s" % (file_path, num))
      return error_messages

def fixHeaderOrder(file_path):
  command = "%s --rewrite %s" % (HEADER_ORDER_PATH, file_path)
  if os.system(command) != 0:
    return ["header_order.py rewrite error: %s" % (file_path)]
  return []

def clangFormat(file_path):
  command = "%s -i %s" % (CLANG_FORMAT_PATH, file_path)
  if os.system(command) != 0:
    return ["clang-format rewrite error: %s" % (file_path)]
  return []

def fixFilePath(file_path):
  if isBuildFile(file_path):
    if os.system("%s %s %s" % (ENVOY_BUILD_FIXER_PATH, file_path, file_path)) != 0:
      return ["envoy_build_fixer rewrite failed for file: %s" % file_path]
    if os.system("%s -mode=fix %s" % (BUILDIFIER_PATH, file_path)) != 0:
      return ["buildifier rewrite failed for file: %s" % file_path]
    return []
  fixFileContents(file_path)
  if file_path.endswith(DOCS_SUFFIX):
    return []

  error_messages = (checkNamespace(file_path) or
                    checkProtobufExternalDepsBuild(file_path) or
                    checkProtobufExternalDeps(file_path))
  if error_messages:
    return error_messages + ["This cannot be automatically corrected. Please fix by hand."]

  error_messages = []
  error_messages += fixHeaderOrder(file_path)
  error_messages += clangFormat(file_path)
  return error_messages

def checkFormat(file_path):
  if file_path.startswith(EXCLUDED_PREFIXES):
    return []

  if not file_path.endswith(SUFFIXES):
    return []

  error_messages = []
  if operation_type == "check":
    error_messages += checkFilePath(file_path)

  if operation_type == "fix":
    error_messages += fixFilePath(file_path)

  if error_messages:
    return ["From %s" % file_path] + error_messages
  return error_messages

def checkFormatReturnTraceOnError(file_path):
  """Run checkFormat and return the traceback of any exception."""
  try:
    return checkFormat(file_path)
  except:
    return traceback.format_exc().split("\n")

def checkFormatVisitor(arg, dir_name, names):
  pool, result_list = arg
  for file_name in names:
    result = pool.apply_async(checkFormatReturnTraceOnError, args=(dir_name + "/" + file_name,))
    result_list.append(result)

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
    error_messages = checkFormat("./" + target_path)
  else:
    pool = multiprocessing.Pool()
    results = []
    os.path.walk(target_path, checkFormatVisitor, [pool, results])
    pool.close()
    pool.join()
    error_messages = sum((r.get() for r in results), [])

  if error_messages:
    for e in error_messages:
      print "ERROR: %s" % e
    print "ERROR: check format failed. run 'tools/check_format.py fix'"
    sys.exit(1)
