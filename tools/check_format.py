#!/usr/bin/env python

import argparse
import common
import fileinput
import multiprocessing
import os
import os.path
import re
import subprocess
import stat
import sys
import traceback

EXCLUDED_PREFIXES = ("./generated/", "./thirdparty/", "./build", "./.git/",
                     "./bazel-", "./bazel/external", "./.cache",
                     "./source/extensions/extensions_build_config.bzl",
                     "./tools/testdata/check_format/")
SUFFIXES = (".cc", ".h", "BUILD", ".bzl", ".md", ".rst", ".proto")
DOCS_SUFFIX = (".md", ".rst")
PROTO_SUFFIX = (".proto")

# Files in these paths can make reference to protobuf stuff directly
GOOGLE_PROTOBUF_WHITELIST = ("ci/prebuilt", "source/common/protobuf", "api/test")
REPOSITORIES_BZL = "bazel/repositories.bzl"

# Files matching these exact names can reference real-world time. These include the class
# definitions for real-world time, the construction of them in main(), and perf annotation.
# For now it includes the validation server but that really should be injected too.
REAL_TIME_WHITELIST = ('./source/common/common/utility.h',
                       './source/common/event/real_time_system.cc',
                       './source/common/event/real_time_system.h',
                       './source/exe/main_common.cc',
                       './source/exe/main_common.h',
                       './source/server/config_validation/server.cc',
                       './source/common/common/perf_annotation.h',
                       './test/test_common/simulated_time_system.cc',
                       './test/test_common/simulated_time_system.h',
                       './test/test_common/test_time.cc',
                       './test/test_common/test_time.h',
                       './test/test_common/utility.cc',
                       './test/test_common/utility.h',
                       './test/integration/integration.h')

CLANG_FORMAT_PATH = os.getenv("CLANG_FORMAT", "clang-format-7")
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
    "Protobuf::ListValue":              "ProtobufWkt::ListValue",
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

# lookPath searches for the given executable in all directories in PATH
# environment variable. If it cannot be found, empty string is returned.
def lookPath(executable):
  for path_dir in os.environ["PATH"].split(os.pathsep):
    executable_path = os.path.join(path_dir, executable)
    if os.path.exists(executable_path):
      return executable_path
  return ""

# pathExists checks whether the given path exists. This function assumes that
# the path is absolute and evaluates environment variables.
def pathExists(executable):
  return os.path.exists(os.path.expandvars(executable))

# executableByOthers checks whether the given path has execute permission for
# others.
def executableByOthers(executable):
  st = os.stat(os.path.expandvars(executable))
  return bool(st.st_mode & stat.S_IXOTH)

# Check whether all needed external tools (clang-format, buildifier) are
# available.
def checkTools():
  error_messages = []

  clang_format_abs_path = lookPath(CLANG_FORMAT_PATH)
  if clang_format_abs_path:
    if not executableByOthers(clang_format_abs_path):
      error_messages.append("command {} exists, but cannot be executed by other "
                            "users".format(CLANG_FORMAT_PATH))
  else:
    error_messages.append(
      "Command {} not found. If you have clang-format in version 7.x.x "
      "installed, but the binary name is different or it's not available in "
      "PATH, please use CLANG_FORMAT environment variable to specify the path. "
      "Examples:\n"
      "    export CLANG_FORMAT=clang-format-7.0.0\n"
      "    export CLANG_FORMAT=/opt/bin/clang-format-7".format(
        CLANG_FORMAT_PATH)
    )

  buildifier_abs_path = lookPath(BUILDIFIER_PATH)
  if buildifier_abs_path:
    if not executableByOthers(buildifier_abs_path):
      error_messages.append("command {} exists, but cannot be executed by other "
                            "users".format(BUILDIFIER_PATH))
  elif pathExists(BUILDIFIER_PATH):
    if not executableByOthers(BUILDIFIER_PATH):
      error_messages.append("command {} exists, but cannot be executed by other "
                            "users".format(BUILDIFIER_PATH))
  else:
    error_messages.append(
      "Command {} not found. If you have buildifier installed, but the binary "
      "name is different or it's not available in $GOPATH/bin, please use "
      "BUILDIFIER_BIN environment variable to specify the path. Example:"
      "    export BUILDIFIER_BIN=/opt/bin/buildifier\n"
      "If you don't have buildifier installed, you can install it by:\n"
      "    go get -u github.com/bazelbuild/buildtools/buildifier".format(
        BUILDIFIER_PATH)
    )

  return error_messages

def checkNamespace(file_path):
  with open(file_path) as f:
    text = f.read()
    if not re.search('^\s*namespace\s+Envoy\s*{', text, re.MULTILINE) and \
       not 'NOLINT(namespace-envoy)' in text:
      return ["Unable to find Envoy namespace or NOLINT(namespace-envoy) for file: %s" % file_path]
  return []

# To avoid breaking the Lyft import, we just check for path inclusion here.
def whitelistedForProtobufDeps(file_path):
  return (file_path.endswith(PROTO_SUFFIX) or file_path.endswith(REPOSITORIES_BZL) or \
          any(path_segment in file_path for path_segment in GOOGLE_PROTOBUF_WHITELIST))

# Real-world time sources should not be instantiated in the source, except for a few
# specific cases. They should be passed down from where they are instantied to where
# they need to be used, e.g. through the ServerInstance, Dispatcher, or ClusterManager.
def whitelistedForRealTime(file_path):
  return file_path in REAL_TIME_WHITELIST

def findSubstringAndReturnError(pattern, file_path, error_message):
  with open(file_path) as f:
    text = f.read()
    if pattern in text:
      error_messages = [file_path + ': ' + error_message]
      for i, line in enumerate(text.splitlines()):
        if pattern in line:
          error_messages.append("  %s:%s" % (file_path, i + 1))
      return error_messages
    return []

def isApiFile(file_path):
  return file_path.startswith(args.api_prefix)

def isBuildFile(file_path):
  basename = os.path.basename(file_path)
  if basename in {"BUILD", "BUILD.bazel"} or basename.endswith(".BUILD"):
    return True
  return False

def isSkylarkFile(file_path):
  return file_path.endswith(".bzl")

def hasInvalidAngleBracketDirectory(line):
  if not line.startswith(INCLUDE_ANGLE):
    return False
  path = line[INCLUDE_ANGLE_LEN:]
  slash = path.find("/")
  if slash == -1:
    return False
  subdir = path[0:slash]
  return subdir in SUBDIR_SET

def checkFileContents(file_path, checker):
  error_messages = []
  for line_number, line in enumerate(fileinput.input(file_path)):
    def reportError(message):
      error_messages.append("%s:%d: %s" % (file_path, line_number + 1, message))
    checker(line, file_path, reportError)
  return error_messages

DOT_MULTI_SPACE_REGEX = re.compile('\\. +')

def fixSourceLine(line):
  # Strip double space after '.'  This may prove overenthusiastic and need to
  # be restricted to comments and metadata files but works for now.
  line = re.sub(DOT_MULTI_SPACE_REGEX, '. ', line)

  if hasInvalidAngleBracketDirectory(line):
    line = line.replace('<', '"').replace(">", '"')

  # Fix incorrect protobuf namespace references.
  for invalid_construct, valid_construct in PROTOBUF_TYPE_ERRORS.items():
    line = line.replace(invalid_construct, valid_construct)

  return line

# We want to look for a call to condvar.waitFor, but there's no strong pattern
# to the variable name of the condvar. If we just look for ".waitFor" we'll also
# pick up time_system_.waitFor(...), and we don't want to return true for that
# pattern. But in that case there is a strong pattern of using time_system in
# various spellings as the variable name.
def hasCondVarWaitFor(line):
  wait_for = line.find('.waitFor(')
  if wait_for == -1:
    return False
  preceding = line[0:wait_for]
  if preceding.endswith('time_system') or preceding.endswith('timeSystem()') or \
     preceding.endswith('time_system_'):
    return False
  return True

def checkSourceLine(line, file_path, reportError):
  # Check fixable errors. These may have been fixed already.
  if line.find(".  ") != -1:
    reportError("over-enthusiastic spaces")
  if hasInvalidAngleBracketDirectory(line):
    reportError("envoy includes should not have angle brackets")
  for invalid_construct, valid_construct in PROTOBUF_TYPE_ERRORS.items():
    if invalid_construct in line:
      reportError("incorrect protobuf type reference %s; "
                  "should be %s" % (invalid_construct, valid_construct))

  # Some errors cannot be fixed automatically, and actionable, consistent,
  # navigable messages should be emitted to make it easy to find and fix
  # the errors by hand.
  if not whitelistedForProtobufDeps(file_path):
    if '"google/protobuf' in line or "google::protobuf" in line:
      reportError("unexpected direct dependency on google.protobuf, use "
                  "the definitions in common/protobuf/protobuf.h instead.")
  if line.startswith('#include <mutex>') or line.startswith('#include <condition_variable'):
    # We don't check here for std::mutex because that may legitimately show up in
    # comments, for example this one.
    reportError("Don't use <mutex> or <condition_variable*>, switch to "
                "Thread::MutexBasicLockable in source/common/common/thread.h")
  if line.startswith('#include <shared_mutex>'):
    # We don't check here for std::shared_timed_mutex because that may
    # legitimately show up in comments, for example this one.
    reportError("Don't use <shared_mutex>, use absl::Mutex for reader/writer locks.")
  if not whitelistedForRealTime(file_path) and not 'NO_CHECK_FORMAT(real_time)' in line:
    if 'RealTimeSource' in line or 'RealTimeSystem' in line or \
       'std::chrono::system_clock::now' in line or 'std::chrono::steady_clock::now' in line or \
       'std::this_thread::sleep_for' in line or hasCondVarWaitFor(line):
      reportError("Don't reference real-world time sources from production code; use injection")
  if 'std::atomic_' in line:
    # The std::atomic_* free functions are functionally equivalent to calling
    # operations on std::atomic<T> objects, so prefer to use that instead.
    reportError("Don't use free std::atomic_* functions, use std::atomic<T> members instead.")
  if '__attribute__((packed))' in line and file_path != './include/envoy/common/platform.h':
    # __attribute__((packed)) is not supported by MSVC, we have a PACKED_STRUCT macro that
    # can be used instead
    reportError("Don't use __attribute__((packed)), use the PACKED_STRUCT macro defined "
                "in include/envoy/common/platform.h instead")
  if re.search("\{\s*\.\w+\s*\=", line):
    # Designated initializers are not part of the C++14 standard and are not supported
    # by MSVC
    reportError("Don't use designated initializers in struct initialization, "
                "they are not part of C++14")
  if ' ?: ' in line:
    # The ?: operator is non-standard, it is a GCC extension
    reportError("Don't use the '?:' operator, it is a non-standard GCC extension")


def checkBuildLine(line, file_path, reportError):
  if not whitelistedForProtobufDeps(file_path) and '"protobuf"' in line:
    reportError("unexpected direct external dependency on protobuf, use "
                "//source/common/protobuf instead.")
  if envoy_build_rule_check and not isSkylarkFile(file_path) and '@envoy//' in line:
    reportError("Superfluous '@envoy//' prefix")

def fixBuildLine(line, file_path):
  if envoy_build_rule_check and not isSkylarkFile(file_path):
    line = line.replace('@envoy//', '//')
  return line

def fixBuildPath(file_path):
  for line in fileinput.input(file_path, inplace=True):
    sys.stdout.write(fixBuildLine(line, file_path))

  error_messages = []
  # TODO(htuch): Add API specific BUILD fixer script.
  if not isApiFile(file_path) and not isSkylarkFile(file_path):
    if os.system("%s %s %s" % (ENVOY_BUILD_FIXER_PATH, file_path, file_path)) != 0:
      error_messages += ["envoy_build_fixer rewrite failed for file: %s" % file_path]

  if os.system("%s -mode=fix %s" % (BUILDIFIER_PATH, file_path)) != 0:
    error_messages += ["buildifier rewrite failed for file: %s" % file_path]
  return error_messages

def checkBuildPath(file_path):
  error_messages = []
  if not isApiFile(file_path) and not isSkylarkFile(file_path):
    command = "%s %s | diff %s -" % (ENVOY_BUILD_FIXER_PATH, file_path, file_path)
    error_messages += executeCommand(command, "envoy_build_fixer check failed", file_path)

  command = "%s -mode=diff %s" % (BUILDIFIER_PATH, file_path)
  error_messages += executeCommand(command, "buildifier check failed", file_path)
  error_messages += checkFileContents(file_path, checkBuildLine)
  return error_messages

def fixSourcePath(file_path):
  for line in fileinput.input(file_path, inplace=True):
    sys.stdout.write(fixSourceLine(line))

  error_messages = []
  if not file_path.endswith(DOCS_SUFFIX):
    if not file_path.endswith(PROTO_SUFFIX):
      error_messages += fixHeaderOrder(file_path)
    error_messages += clangFormat(file_path)
  return error_messages

def checkSourcePath(file_path):
  error_messages = checkFileContents(file_path, checkSourceLine)

  if not file_path.endswith(DOCS_SUFFIX):
    if not file_path.endswith(PROTO_SUFFIX):
      error_messages += checkNamespace(file_path)
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

def checkFormat(file_path):
  if file_path.startswith(EXCLUDED_PREFIXES):
    return []

  if not file_path.endswith(SUFFIXES):
    return []

  error_messages = []
  # Apply fixes first, if asked, and then run checks. If we wind up attempting to fix
  # an issue, but there's still an error, that's a problem.
  try_to_fix = operation_type == "fix"
  if isBuildFile(file_path) or isSkylarkFile(file_path):
    if try_to_fix:
      error_messages += fixBuildPath(file_path)
    error_messages += checkBuildPath(file_path)
  else:
    if try_to_fix:
      error_messages += fixSourcePath(file_path)
    error_messages += checkSourcePath(file_path)

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
  """Run checkFormat in parallel for the given files.

  Args:
    arg: a tuple (pool, result_list) for starting tasks asynchronously.
    dir_name: the parent directory of the given files.
    names: a list of file names.
  """

  # Unpack the multiprocessing.Pool process pool and list of results. Since
  # python lists are passed as references, this is used to collect the list of
  # async results (futures) from running checkFormat and passing them back to
  # the caller.
  pool, result_list = arg
  for file_name in names:
    result = pool.apply_async(checkFormatReturnTraceOnError, args=(dir_name + "/" + file_name,))
    result_list.append(result)

# checkErrorMessages iterates over the list with error messages and prints
# errors and returns a bool based on whether there were any errors.
def checkErrorMessages(error_messages):
  if error_messages:
    for e in error_messages:
      print "ERROR: %s" % e
    return True
  return False

if __name__ == "__main__":
  parser = argparse.ArgumentParser(description='Check or fix file format.')
  parser.add_argument('operation_type', type=str, choices=['check', 'fix'],
                      help="specify if the run should 'check' or 'fix' format.")
  parser.add_argument('target_path', type=str, nargs="?", default=".",
                      help="specify the root directory for the script to recurse over. Default '.'.")
  parser.add_argument('--add-excluded-prefixes', type=str, nargs="+",
                      help="exclude additional prefixes.")
  parser.add_argument('-j', '--num-workers', type=int, default=multiprocessing.cpu_count(),
                      help="number of worker processes to use; defaults to one per core.")
  parser.add_argument('--api-prefix', type=str, default='./api/', help="path of the API tree")
  parser.add_argument('--skip_envoy_build_rule_check', action='store_true',
                      help="Skip checking for '@envoy//' prefix in build rules.")
  args = parser.parse_args()

  operation_type = args.operation_type
  target_path = args.target_path
  envoy_build_rule_check = not args.skip_envoy_build_rule_check
  if args.add_excluded_prefixes:
    EXCLUDED_PREFIXES += tuple(args.add_excluded_prefixes)

  # Check whether all needed external tools are available.
  ct_error_messages = checkTools()
  if checkErrorMessages(ct_error_messages):
    sys.exit(1)

  if os.path.isfile(target_path):
    error_messages = checkFormat("./" + target_path)
  else:
    pool = multiprocessing.Pool(processes=args.num_workers)
    results = []
    # For each file in target_path, start a new task in the pool and collect the
    # results (results is passed by reference, and is used as an output).
    os.path.walk(target_path, checkFormatVisitor, (pool, results))

    # Close the pool to new tasks, wait for all of the running tasks to finish,
    # then collect the error messages.
    pool.close()
    pool.join()
    error_messages = sum((r.get() for r in results), [])

  if checkErrorMessages(error_messages):
    print "ERROR: check format failed. run 'tools/check_format.py fix'"
    sys.exit(1)

  if operation_type == "check":
    print "PASS"
