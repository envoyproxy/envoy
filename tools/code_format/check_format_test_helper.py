#!/usr/bin/env python3

# Tests check_format.py. This must be run in a context where the clang
# version and settings are compatible with the one in the Envoy
# docker. Normally this is run via check_format_test.sh, which
# executes it in under docker.

from __future__ import print_function

from run_command import runCommand
import argparse
import logging
import os
import shutil
import sys
import tempfile

curr_dir = os.path.dirname(os.path.realpath(__file__))
tools = os.path.dirname(curr_dir)
src = os.path.join(tools, 'testdata', 'check_format')
check_format = sys.executable + " " + os.path.join(curr_dir, 'check_format.py')
errors = 0


# Runs the 'check_format' operation, on the specified file, printing
# the comamnd run and the status code as well as the stdout, and returning
# all of that to the caller.
def runCheckFormat(operation, filename):
  command = check_format + " " + operation + " " + filename
  status, stdout, stderr = runCommand(command)
  return (command, status, stdout + stderr)


def getInputFile(filename, extra_input_files=None):
  files_to_copy = [filename]
  if extra_input_files is not None:
    files_to_copy.extend(extra_input_files)
  for f in files_to_copy:
    infile = os.path.join(src, f)
    directory = os.path.dirname(f)
    if not directory == '' and not os.path.isdir(directory):
      os.makedirs(directory)
    shutil.copyfile(infile, f)
  return filename


# Attempts to fix file, returning a 4-tuple: the command, input file name,
# output filename, captured stdout as an array of lines, and the error status
# code.
def fixFileHelper(filename, extra_input_files=None):
  command, status, stdout = runCheckFormat(
      "fix", getInputFile(filename, extra_input_files=extra_input_files))
  infile = os.path.join(src, filename)
  return command, infile, filename, status, stdout


# Attempts to fix a file, returning the status code and the generated output.
# If the fix was successful, the diff is returned as a string-array. If the file
# was not fixable, the error-messages are returned as a string-array.
def fixFileExpectingSuccess(file, extra_input_files=None):
  command, infile, outfile, status, stdout = fixFileHelper(file,
                                                           extra_input_files=extra_input_files)
  if status != 0:
    print("FAILED: " + infile)
    emitStdoutAsError(stdout)
    return 1
  status, stdout, stderr = runCommand('diff ' + outfile + ' ' + infile + '.gold')
  if status != 0:
    print("FAILED: " + infile)
    emitStdoutAsError(stdout + stderr)
    return 1
  return 0


def fixFileExpectingNoChange(file):
  command, infile, outfile, status, stdout = fixFileHelper(file)
  if status != 0:
    return 1
  status, stdout, stderr = runCommand('diff ' + outfile + ' ' + infile)
  if status != 0:
    logging.error(file + ': expected file to remain unchanged')
    return 1
  return 0


def emitStdoutAsError(stdout):
  logging.error("\n".join(stdout))


def expectError(filename, status, stdout, expected_substring):
  if status == 0:
    logging.error("%s: Expected failure `%s`, but succeeded" % (filename, expected_substring))
    return 1
  for line in stdout:
    if expected_substring in line:
      return 0
  logging.error("%s: Could not find '%s' in:\n" % (filename, expected_substring))
  emitStdoutAsError(stdout)
  return 1


def fixFileExpectingFailure(filename, expected_substring):
  command, infile, outfile, status, stdout = fixFileHelper(filename)
  return expectError(filename, status, stdout, expected_substring)


def checkFileExpectingError(filename, expected_substring, extra_input_files=None):
  command, status, stdout = runCheckFormat(
      "check", getInputFile(filename, extra_input_files=extra_input_files))
  return expectError(filename, status, stdout, expected_substring)


def checkAndFixError(filename, expected_substring, extra_input_files=None):
  errors = checkFileExpectingError(filename,
                                   expected_substring,
                                   extra_input_files=extra_input_files)
  errors += fixFileExpectingSuccess(filename, extra_input_files=extra_input_files)
  return errors


def checkToolNotFoundError():
  # Temporarily change PATH to test the error about lack of external tools.
  oldPath = os.environ["PATH"]
  os.environ["PATH"] = "/sbin:/usr/sbin"
  clang_format = os.getenv("CLANG_FORMAT", "clang-format-9")
  # If CLANG_FORMAT points directly to the binary, skip this test.
  if os.path.isfile(clang_format) and os.access(clang_format, os.X_OK):
    os.environ["PATH"] = oldPath
    return 0
  errors = checkFileExpectingError("no_namespace_envoy.cc", "Command %s not found." % clang_format)
  os.environ["PATH"] = oldPath
  return errors


def checkUnfixableError(filename, expected_substring):
  errors = checkFileExpectingError(filename, expected_substring)
  errors += fixFileExpectingFailure(filename, expected_substring)
  return errors


def checkFileExpectingOK(filename):
  command, status, stdout = runCheckFormat("check", getInputFile(filename))
  if status != 0:
    logging.error("Expected %s to have no errors; status=%d, output:\n" % (filename, status))
    emitStdoutAsError(stdout)
  return status + fixFileExpectingNoChange(filename)


def runChecks():
  errors = 0

  # The following error is the error about unavailability of external tools.
  errors += checkToolNotFoundError()

  # The following errors can be detected but not fixed automatically.
  errors += checkUnfixableError("no_namespace_envoy.cc",
                                "Unable to find Envoy namespace or NOLINT(namespace-envoy)")
  errors += checkUnfixableError("mutex.cc", "Don't use <mutex> or <condition_variable*>")
  errors += checkUnfixableError("condition_variable.cc",
                                "Don't use <mutex> or <condition_variable*>")
  errors += checkUnfixableError("condition_variable_any.cc",
                                "Don't use <mutex> or <condition_variable*>")
  errors += checkUnfixableError("shared_mutex.cc", "shared_mutex")
  errors += checkUnfixableError("shared_mutex.cc", "shared_mutex")
  real_time_inject_error = (
      "Don't reference real-world time sources from production code; use injection")
  errors += checkUnfixableError("real_time_source.cc", real_time_inject_error)
  errors += checkUnfixableError("real_time_system.cc", real_time_inject_error)
  errors += checkUnfixableError(
      "duration_value.cc",
      "Don't use ambiguous duration(value), use an explicit duration type, e.g. Event::TimeSystem::Milliseconds(value)"
  )
  errors += checkUnfixableError("system_clock.cc", real_time_inject_error)
  errors += checkUnfixableError("steady_clock.cc", real_time_inject_error)
  errors += checkUnfixableError(
      "unpack_to.cc", "Don't use UnpackTo() directly, use MessageUtil::unpackTo() instead")
  errors += checkUnfixableError("condvar_wait_for.cc", real_time_inject_error)
  errors += checkUnfixableError("sleep.cc", real_time_inject_error)
  errors += checkUnfixableError("std_atomic_free_functions.cc", "std::atomic_*")
  errors += checkUnfixableError("std_get_time.cc", "std::get_time")
  errors += checkUnfixableError("no_namespace_envoy.cc",
                                "Unable to find Envoy namespace or NOLINT(namespace-envoy)")
  errors += checkUnfixableError("bazel_tools.BUILD", "unexpected @bazel_tools reference")
  errors += checkUnfixableError("proto.BUILD", "unexpected direct external dependency on protobuf")
  errors += checkUnfixableError("proto_deps.cc", "unexpected direct dependency on google.protobuf")
  errors += checkUnfixableError("attribute_packed.cc", "Don't use __attribute__((packed))")
  errors += checkUnfixableError("designated_initializers.cc", "Don't use designated initializers")
  errors += checkUnfixableError("elvis_operator.cc", "Don't use the '?:' operator")
  errors += checkUnfixableError("testing_test.cc",
                                "Don't use 'using testing::Test;, elaborate the type instead")
  errors += checkUnfixableError(
      "serialize_as_string.cc",
      "Don't use MessageLite::SerializeAsString for generating deterministic serialization")
  errors += checkUnfixableError(
      "version_history/current.rst",
      "Version history not in alphabetical order (zzzzz vs aaaaa): please check placement of line")
  errors += checkUnfixableError(
      "version_history/current.rst",
      "Version history not in alphabetical order (this vs aaaa): please check placement of line")
  errors += checkUnfixableError(
      "version_history/current.rst",
      "Version history line malformed. Does not match VERSION_HISTORY_NEW_LINE_REGEX in "
      "check_format.py")
  errors += checkUnfixableError(
      "counter_from_string.cc",
      "Don't lookup stats by name at runtime; use StatName saved during construction")
  errors += checkUnfixableError(
      "gauge_from_string.cc",
      "Don't lookup stats by name at runtime; use StatName saved during construction")
  errors += checkUnfixableError(
      "histogram_from_string.cc",
      "Don't lookup stats by name at runtime; use StatName saved during construction")
  errors += checkUnfixableError(
      "regex.cc", "Don't use std::regex in code that handles untrusted input. Use RegexMatcher")
  errors += checkUnfixableError(
      "grpc_init.cc",
      "Don't call grpc_init() or grpc_shutdown() directly, instantiate Grpc::GoogleGrpcContext. " +
      "See #8282")
  errors += checkUnfixableError(
      "grpc_shutdown.cc",
      "Don't call grpc_init() or grpc_shutdown() directly, instantiate Grpc::GoogleGrpcContext. " +
      "See #8282")
  errors += checkUnfixableError("clang_format_double_off.cc", "clang-format nested off")
  errors += checkUnfixableError("clang_format_trailing_off.cc", "clang-format remains off")
  errors += checkUnfixableError("clang_format_double_on.cc", "clang-format nested on")
  errors += fixFileExpectingFailure(
      "api/missing_package.proto",
      "Unable to find package name for proto file: ./api/missing_package.proto")
  errors += checkUnfixableError("proto_enum_mangling.cc",
                                "Don't use mangled Protobuf names for enum constants")
  errors += checkUnfixableError("test_naming.cc",
                                "Test names should be CamelCase, starting with a capital letter")
  errors += checkUnfixableError(
      "test/register_factory.cc",
      "Don't use Registry::RegisterFactory or REGISTER_FACTORY in tests, use "
      "Registry::InjectFactory instead.")
  errors += checkUnfixableError("strerror.cc",
                                "Don't use strerror; use Envoy::errorDetails instead")
  errors += checkUnfixableError(
      "std_unordered_map.cc", "Don't use std::unordered_map; use absl::flat_hash_map instead " +
      "or absl::node_hash_map if pointer stability of keys/values is required")
  errors += checkUnfixableError(
      "std_unordered_set.cc", "Don't use std::unordered_set; use absl::flat_hash_set instead " +
      "or absl::node_hash_set if pointer stability of keys/values is required")
  errors += checkUnfixableError("std_any.cc", "Don't use std::any; use absl::any instead")
  errors += checkUnfixableError("std_get_if.cc", "Don't use std::get_if; use absl::get_if instead")
  errors += checkUnfixableError(
      "std_holds_alternative.cc",
      "Don't use std::holds_alternative; use absl::holds_alternative instead")
  errors += checkUnfixableError("std_make_optional.cc",
                                "Don't use std::make_optional; use absl::make_optional instead")
  errors += checkUnfixableError("std_monostate.cc",
                                "Don't use std::monostate; use absl::monostate instead")
  errors += checkUnfixableError("std_optional.cc",
                                "Don't use std::optional; use absl::optional instead")
  errors += checkUnfixableError("std_string_view.cc",
                                "Don't use std::string_view; use absl::string_view instead")
  errors += checkUnfixableError("std_variant.cc",
                                "Don't use std::variant; use absl::variant instead")
  errors += checkUnfixableError("std_visit.cc", "Don't use std::visit; use absl::visit instead")
  errors += checkUnfixableError(
      "throw.cc", "Don't introduce throws into exception-free files, use error statuses instead.")
  errors += checkUnfixableError("pgv_string.proto", "min_bytes is DEPRECATED, Use min_len.")
  errors += checkFileExpectingOK("commented_throw.cc")
  errors += checkUnfixableError("repository_url.bzl",
                                "Only repository_locations.bzl may contains URL references")
  errors += checkUnfixableError("repository_urls.bzl",
                                "Only repository_locations.bzl may contains URL references")

  # The following files have errors that can be automatically fixed.
  errors += checkAndFixError("over_enthusiastic_spaces.cc",
                             "./over_enthusiastic_spaces.cc:3: over-enthusiastic spaces")
  errors += checkAndFixError("extra_enthusiastic_spaces.cc",
                             "./extra_enthusiastic_spaces.cc:3: over-enthusiastic spaces")
  errors += checkAndFixError("angle_bracket_include.cc",
                             "envoy includes should not have angle brackets")
  errors += checkAndFixError("proto_style.cc", "incorrect protobuf type reference")
  errors += checkAndFixError("long_line.cc", "clang-format check failed")
  errors += checkAndFixError("header_order.cc", "header_order.py check failed")
  errors += checkAndFixError("clang_format_on.cc",
                             "./clang_format_on.cc:7: over-enthusiastic spaces")
  # Validate that a missing license is added.
  errors += checkAndFixError("license.BUILD", "envoy_build_fixer check failed")
  # Validate that an incorrect license is replaced and reordered.
  errors += checkAndFixError("update_license.BUILD", "envoy_build_fixer check failed")
  # Validate that envoy_package() is added where there is an envoy_* rule occurring.
  errors += checkAndFixError("add_envoy_package.BUILD", "envoy_build_fixer check failed")
  # Validate that we don't add envoy_package() when no envoy_* rule.
  errors += checkFileExpectingOK("skip_envoy_package.BUILD")
  # Validate that we clean up gratuitous blank lines.
  errors += checkAndFixError("canonical_spacing.BUILD", "envoy_build_fixer check failed")
  # Validate that unused loads are removed.
  errors += checkAndFixError("remove_unused_loads.BUILD", "envoy_build_fixer check failed")
  # Validate that API proto package deps are computed automagically.
  errors += checkAndFixError("canonical_api_deps.BUILD",
                             "envoy_build_fixer check failed",
                             extra_input_files=[
                                 "canonical_api_deps.cc", "canonical_api_deps.h",
                                 "canonical_api_deps.other.cc"
                             ])
  errors += checkAndFixError("bad_envoy_build_sys_ref.BUILD", "Superfluous '@envoy//' prefix")
  errors += checkAndFixError("proto_format.proto", "clang-format check failed")
  errors += checkAndFixError(
      "cpp_std.cc",
      "term absl::make_unique< should be replaced with standard library term std::make_unique<")

  errors += checkFileExpectingOK("real_time_source_override.cc")
  errors += checkFileExpectingOK("duration_value_zero.cc")
  errors += checkFileExpectingOK("time_system_wait_for.cc")
  errors += checkFileExpectingOK("clang_format_off.cc")
  return errors


if __name__ == "__main__":
  parser = argparse.ArgumentParser(description='tester for check_format.py.')
  parser.add_argument('--log', choices=['INFO', 'WARN', 'ERROR'], default='INFO')
  args = parser.parse_args()
  logging.basicConfig(format='%(message)s', level=args.log)

  # Now create a temp directory to copy the input files, so we can fix them
  # without actually fixing our testdata. This requires chdiring to the temp
  # directory, so it's annoying to comingle check-tests and fix-tests.
  with tempfile.TemporaryDirectory() as tmp:
    os.chdir(tmp)
    errors = runChecks()

  if errors != 0:
    logging.error("%d FAILURES" % errors)
    exit(1)
  logging.warning("PASS")
