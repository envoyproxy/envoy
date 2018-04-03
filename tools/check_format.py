#!/usr/bin/env python

import argparse
import common
import fileinput
import os
import os.path
import re
import subprocess
import sys

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
SUBDIR_SET = set(common.includeDirOrder())
INCLUDE_ANGLE = "#include <"
INCLUDE_ANGLE_LEN = len(INCLUDE_ANGLE)

PROTOBUF_TYPE_ERRORS = {
    # Well-known types should be referenced from the ProtobufWkt namespace.
    "Protobuf::Any":                    "ProtobufWkt::Any",
    "Protobuf::Empty":                  "ProtobufWkt::Empty",
    "Protobuf::ListValue":              "ProtobufWkt:ListValue",
    "Protobuf::NULL_VALUE":             "ProtobufWkt::NULL_VALUE",
    "Protobuf::StringValue":            "ProtobufWkt::StringValue",
    "Protobuf::Struct":                 "ProtobufWkt::Struct",
    "Protobuf::Value":                  "ProtobufWkt::Value",

    # Maps including strings should use the protobuf string types.
    "Protobuf::MapPair<std::string":    "Protobuf::MapPair<Envoy::ProtobufTypes::String",

    # Other common mis-namespacing of protobuf types.
    "ProtobufWkt::Map":                 "Protobuf::Map",
    "ProtobufWkt::MapPair":             "Protobuf::MapPair",
    "ProtobufUtil::MessageDifferencer": "Protobuf::util::MessageDifferencer"
}

found_error = False


def printError(error):
  global found_error
  found_error = True
  print "ERROR: %s" % (error)


def checkNamespace(file_path):
  with open(file_path) as f:
    text = f.read()
    if not re.search('^\s*namespace\s+Envoy\s*{', text, re.MULTILINE) and \
       not 'NOLINT(namespace-envoy)' in text:
      printError("Unable to find Envoy namespace or NOLINT(namespace-envoy) for file: %s" % file_path)
      return False
  return True


# To avoid breaking the Lyft import, we just check for path inclusion here.
def whitelistedForProtobufDeps(file_path):
  return any(path_segment in file_path for path_segment in GOOGLE_PROTOBUF_WHITELIST)

def findSubstringAndPrintError(pattern, file_path, error_message):
  with open(file_path) as f:
    text = f.read()
    if pattern in text:
      printError(file_path + ': ' + error_message)
      for i, line in enumerate(text.splitlines()):
        if pattern in line:
          printError("  %s:%s" % (file_path, i + 1))
      return False
    return True

def checkProtobufExternalDepsBuild(file_path):
  if whitelistedForProtobufDeps(file_path):
    return True
  message = ("unexpected direct external dependency on protobuf, use "
    "//source/common/protobuf instead.")
  return findSubstringAndPrintError('"protobuf"', file_path, message)


def checkProtobufExternalDeps(file_path):
  if whitelistedForProtobufDeps(file_path):
    return True
  with open(file_path) as f:
    text = f.read()
    if '"google/protobuf' in text or "google::protobuf" in text:
      printError(
          "%s has unexpected direct dependency on google.protobuf, use "
          "the definitions in common/protobuf/protobuf.h instead." % file_path)
      return False
    return True


def isBuildFile(file_path):
  basename = os.path.basename(file_path)
  if basename in {"BUILD", "BUILD.bazel"} or basename.endswith(".BUILD"):
    return True
  return False

def hasInvalidAngleBracketDirectory(line):
  if not line.startswith(INCLUDE_ANGLE):
    return False
  path = line[INCLUDE_ANGLE_LEN:]
  slash = path.find("/")
  if slash == -1:
    return False
  subdir = path[0:slash]
  return subdir in SUBDIR_SET

def printLineError(path, zero_based_line_number, message):
  printError("%s:%d: %s" % (path, zero_based_line_number + 1, message))

def checkFileContents(file_path):
  for line_number, line in enumerate(fileinput.input(file_path)):
    if line.find(".  ") != -1:
      printLineError(file_path, line_number, "over-enthusiastic spaces")
    if hasInvalidAngleBracketDirectory(line):
      printLineError(file_path, line_number, "envoy includes should not have angle brackets")

def fixFileContents(file_path):
  for line in fileinput.input(file_path, inplace=True):
    # Strip double space after '.'  This may prove overenthusiastic and need to
    # be restricted to comments and metadata files but works for now.
    line = line.replace('.  ', '. ')

    if hasInvalidAngleBracketDirectory(line):
      line = line.replace('<', '"').replace(">", '"')

    # Fix incorrect protobuf namespace references.
    for error, replacement in PROTOBUF_TYPE_ERRORS.items():
      line = line.replace(error, replacement)

    sys.stdout.write(str(line))

def checkFilePath(file_path):
  if isBuildFile(file_path):
    command = "%s %s | diff %s -" % (ENVOY_BUILD_FIXER_PATH, file_path, file_path)
    executeCommand(command, "envoy_build_fixer check failed", file_path)
    command = "cat %s | %s -mode=fix | diff %s -" % (file_path, BUILDIFIER_PATH, file_path)
    executeCommand(command, "buildifier check failed", file_path)
    checkProtobufExternalDepsBuild(file_path)
    return
  checkFileContents(file_path)

  if file_path.endswith(DOCS_SUFFIX):
    return
  checkNamespace(file_path)
  checkProtobufExternalDeps(file_path)
  command = ("%s %s | diff %s -" % (HEADER_ORDER_PATH, file_path,
                                                   file_path))
  executeCommand(command, "header_order.py check failed", file_path)

  command = ("%s %s | diff %s -" % (CLANG_FORMAT_PATH, file_path,
                                                   file_path))
  executeCommand(command, "clang-format check failed", file_path)

# Example target outputs are:
#   - "26,27c26"
#   - "12,13d13"
#   - "7a8,9"
def executeCommand(command, error_message, file_path,
        regex=re.compile(r"^(\d+)[a|c|d]?\d*(?:,\d+[a|c|d]?\d*)?$")):
  try:
    subprocess.check_output(command, shell=True, stderr=subprocess.STDOUT)
  except subprocess.CalledProcessError as e:
      if (e.returncode != 0 and e.returncode != 1):
          print "ERROR: something went wrong while executing: %s" % e.cmd
          sys.exit(1)
      # In case we can't find any line numbers, call printError at first.
      printError("%s for file: %s" % (error_message, file_path))
      for line in e.output.splitlines():
        for num in regex.findall(line):
          printError("  %s:%s" % (file_path, num))

def fixFilePath(file_path):
  if isBuildFile(file_path):
    if os.system("%s %s %s" % (ENVOY_BUILD_FIXER_PATH, file_path, file_path)) != 0:
      printError("envoy_build_fixer rewrite failed for file: %s" % file_path)
    if os.system("%s -mode=fix %s" % (BUILDIFIER_PATH, file_path)) != 0:
      printError("buildifier rewrite failed for file: %s" % file_path)
    return
  fixFileContents(file_path)
  if file_path.endswith(DOCS_SUFFIX):
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
    os.path.walk(target_path, checkFormatVisitor, None)

  if found_error:
    print "ERROR: check format failed. run 'tools/check_format.py fix'"
    sys.exit(1)
