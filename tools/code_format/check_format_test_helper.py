#!/usr/bin/env python3

# Tests check_format.py. This must be run in a context where the clang
# version and settings are compatible with the one in the Envoy
# docker. Normally this is run via check_format_test.sh, which
# executes it in under docker.

from __future__ import print_function

from run_command import run_command
import argparse
import logging
import os
import shutil
import sys
import tempfile

curr_dir = os.path.dirname(os.path.realpath(__file__))
tools = os.path.dirname(curr_dir)
src = os.path.join(tools, 'testdata', 'check_format')
check_format = f"{sys.executable}  {os.path.join(curr_dir, 'check_format.py')}"
check_format_config = f"{os.path.join(curr_dir, 'config.yaml')}"
errors = 0


# Runs the 'check_format' operation, on the specified file, printing
# the comamnd run and the status code as well as the stdout, and returning
# all of that to the caller.
def run_check_format(operation, filename):
    command = f"{check_format} {operation} {filename} --config_path={check_format_config}"
    status, stdout, stderr = run_command(command)
    return (command, status, stdout + stderr)


def get_input_file(filename, extra_input_files=None):
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
def fix_file_helper(filename, extra_input_files=None):
    command, status, stdout = run_check_format(
        "fix", get_input_file(filename, extra_input_files=extra_input_files))
    infile = os.path.join(src, filename)
    return command, infile, filename, status, stdout


# Attempts to fix a file, returning the status code and the generated output.
# If the fix was successful, the diff is returned as a string-array. If the file
# was not fixable, the error-messages are returned as a string-array.
def fix_file_expecting_success(file, extra_input_files=None):
    command, infile, outfile, status, stdout = fix_file_helper(
        file, extra_input_files=extra_input_files)
    if status != 0:
        print("FAILED: " + infile)
        emit_stdout_as_error(stdout)
        return 1
    status, stdout, stderr = run_command('diff ' + outfile + ' ' + infile + '.gold')
    if status != 0:
        print("FAILED: " + infile)
        emit_stdout_as_error(stdout + stderr)
        return 1
    return 0


def fix_file_expecting_no_change(file):
    command, infile, outfile, status, stdout = fix_file_helper(file)
    if status != 0:
        return 1
    status, stdout, stderr = run_command('diff ' + outfile + ' ' + infile)
    if status != 0:
        logging.error(file + ': expected file to remain unchanged')
        return 1
    return 0


def emit_stdout_as_error(stdout):
    logging.error("\n".join(stdout))


def expect_error(filename, status, stdout, expected_substring):
    if status == 0:
        logging.error("%s: Expected failure `%s`, but succeeded" % (filename, expected_substring))
        return 1
    for line in stdout:
        if expected_substring in line:
            return 0
    logging.error("%s: Could not find '%s' in:\n" % (filename, expected_substring))
    emit_stdout_as_error(stdout)
    return 1


def fix_file_expecting_failure(filename, expected_substring):
    command, infile, outfile, status, stdout = fix_file_helper(filename)
    return expect_error(filename, status, stdout, expected_substring)


def check_file_expecting_error(filename, expected_substring, extra_input_files=None):
    command, status, stdout = run_check_format(
        "check", get_input_file(filename, extra_input_files=extra_input_files))
    return expect_error(filename, status, stdout, expected_substring)


def check_and_fix_error(filename, expected_substring, extra_input_files=None):
    errors = check_file_expecting_error(
        filename, expected_substring, extra_input_files=extra_input_files)
    errors += fix_file_expecting_success(filename, extra_input_files=extra_input_files)
    return errors


def check_tool_not_found_error():
    # Temporarily change PATH to test the error about lack of external tools.
    oldPath = os.environ["PATH"]
    os.environ["PATH"] = "/sbin:/usr/sbin"
    clang_format = os.getenv("CLANG_FORMAT", "clang-format")
    # If CLANG_FORMAT points directly to the binary, skip this test.
    if os.path.isfile(clang_format) and os.access(clang_format, os.X_OK):
        os.environ["PATH"] = oldPath
        return 0
    errors = check_file_expecting_error(
        "no_namespace_envoy.cc", "Command %s not found." % clang_format)
    os.environ["PATH"] = oldPath
    return errors


def check_unfixable_error(filename, expected_substring):
    errors = check_file_expecting_error(filename, expected_substring)
    errors += fix_file_expecting_failure(filename, expected_substring)
    return errors


def check_file_expecting_ok(filename):
    command, status, stdout = run_check_format("check", get_input_file(filename))
    if status != 0:
        logging.error("Expected %s to have no errors; status=%d, output:\n" % (filename, status))
        emit_stdout_as_error(stdout)
    return status + fix_file_expecting_no_change(filename)


def run_checks():
    errors = 0

    # The following error is the error about unavailability of external tools.
    errors += check_tool_not_found_error()

    # The following errors can be detected but not fixed automatically.
    errors += check_unfixable_error(
        "no_namespace_envoy.cc", "Unable to find Envoy namespace or NOLINT(namespace-envoy)")
    errors += check_unfixable_error("mutex.cc", "Don't use <mutex> or <condition_variable*>")
    errors += check_unfixable_error(
        "condition_variable.cc", "Don't use <mutex> or <condition_variable*>")
    errors += check_unfixable_error(
        "condition_variable_any.cc", "Don't use <mutex> or <condition_variable*>")
    errors += check_unfixable_error("shared_mutex.cc", "shared_mutex")
    errors += check_unfixable_error("shared_mutex.cc", "shared_mutex")
    real_time_inject_error = (
        "Don't reference real-world time sources; use TimeSystem::advanceTime(Wait|Async)")
    errors += check_unfixable_error("real_time_source.cc", real_time_inject_error)
    errors += check_unfixable_error("real_time_system.cc", real_time_inject_error)
    errors += check_unfixable_error(
        "duration_value.cc",
        "Don't use ambiguous duration(value), use an explicit duration type, e.g. Event::TimeSystem::Milliseconds(value)"
    )
    errors += check_unfixable_error("system_clock.cc", real_time_inject_error)
    errors += check_unfixable_error("steady_clock.cc", real_time_inject_error)
    errors += check_unfixable_error(
        "unpack_to.cc", "Don't use UnpackTo() directly, use MessageUtil::unpackToNoThrow() instead")
    errors += check_unfixable_error(
        "condvar_wait_for.cc", "Don't use CondVar::waitFor(); use TimeSystem::waitFor() instead.")
    errors += check_unfixable_error("sleep.cc", real_time_inject_error)
    errors += check_unfixable_error("std_atomic_free_functions.cc", "std::atomic_*")
    errors += check_unfixable_error("std_get_time.cc", "std::get_time")
    errors += check_unfixable_error(
        "no_namespace_envoy.cc", "Unable to find Envoy namespace or NOLINT(namespace-envoy)")
    errors += check_unfixable_error("bazel_tools.BUILD", "unexpected @bazel_tools reference")
    errors += check_unfixable_error(
        "proto.BUILD", "unexpected direct external dependency on protobuf")
    errors += check_unfixable_error(
        "proto_deps.cc", "unexpected direct dependency on google.protobuf")
    errors += check_unfixable_error("attribute_packed.cc", "Don't use __attribute__((packed))")
    errors += check_unfixable_error(
        "designated_initializers.cc", "Don't use designated initializers")
    errors += check_unfixable_error("elvis_operator.cc", "Don't use the '?:' operator")
    errors += check_unfixable_error(
        "testing_test.cc", "Don't use 'using testing::Test;, elaborate the type instead")
    errors += check_unfixable_error(
        "serialize_as_string.cc",
        "Don't use MessageLite::SerializeAsString for generating deterministic serialization")
    errors += check_unfixable_error(
        "counter_from_string.cc",
        "Don't lookup stats by name at runtime; use StatName saved during construction")
    errors += check_unfixable_error(
        "gauge_from_string.cc",
        "Don't lookup stats by name at runtime; use StatName saved during construction")
    errors += check_unfixable_error(
        "histogram_from_string.cc",
        "Don't lookup stats by name at runtime; use StatName saved during construction")
    errors += check_unfixable_error(
        "regex.cc", "Don't use std::regex in code that handles untrusted input. Use RegexMatcher")
    errors += check_unfixable_error(
        "grpc_init.cc",
        "Don't call grpc_init() or grpc_shutdown() directly, instantiate Grpc::GoogleGrpcContext. "
        + "See #8282")
    errors += check_unfixable_error(
        "grpc_shutdown.cc",
        "Don't call grpc_init() or grpc_shutdown() directly, instantiate Grpc::GoogleGrpcContext. "
        + "See #8282")
    errors += check_unfixable_error(
        "source/raw_try.cc",
        "Don't use raw try, use TRY_ASSERT_MAIN_THREAD if on the main thread otherwise don't use exceptions."
    )
    errors += check_unfixable_error("clang_format_double_off.cc", "clang-format nested off")
    errors += check_unfixable_error("clang_format_trailing_off.cc", "clang-format remains off")
    errors += check_unfixable_error("clang_format_double_on.cc", "clang-format nested on")
    errors += check_unfixable_error(
        "proto_enum_mangling.cc", "Don't use mangled Protobuf names for enum constants")
    errors += check_unfixable_error(
        "test_naming.cc", "Test names should be CamelCase, starting with a capital letter")
    errors += check_unfixable_error("mock_method_n.cc", "use MOCK_METHOD() instead")
    errors += check_unfixable_error("for_each_n.cc", "use an alternative for loop instead")
    errors += check_unfixable_error(
        "test/register_factory.cc",
        "Don't use Registry::RegisterFactory or REGISTER_FACTORY in tests, use "
        "Registry::InjectFactory instead.")
    errors += check_unfixable_error(
        "strerror.cc", "Don't use strerror; use Envoy::errorDetails instead")
    errors += check_unfixable_error(
        "std_unordered_map.cc", "Don't use std::unordered_map; use absl::flat_hash_map instead "
        + "or absl::node_hash_map if pointer stability of keys/values is required")
    errors += check_unfixable_error(
        "std_unordered_set.cc", "Don't use std::unordered_set; use absl::flat_hash_set instead "
        + "or absl::node_hash_set if pointer stability of keys/values is required")
    errors += check_unfixable_error("std_any.cc", "Don't use std::any; use absl::any instead")
    errors += check_unfixable_error(
        "std_get_if.cc", "Don't use std::get_if; use absl::get_if instead")
    errors += check_unfixable_error(
        "std_holds_alternative.cc",
        "Don't use std::holds_alternative; use absl::holds_alternative instead")
    errors += check_unfixable_error(
        "std_make_optional.cc", "Don't use std::make_optional; use absl::make_optional instead")
    errors += check_unfixable_error(
        "std_monostate.cc", "Don't use std::monostate; use absl::monostate instead")
    errors += check_unfixable_error(
        "std_optional.cc", "Don't use std::optional; use absl::optional instead")
    errors += check_unfixable_error(
        "std_string_view.cc",
        "Don't use std::string_view or toStdStringView; use absl::string_view instead")
    errors += check_unfixable_error(
        "std_variant.cc", "Don't use std::variant; use absl::variant instead")
    errors += check_unfixable_error("std_visit.cc", "Don't use std::visit; use absl::visit instead")
    errors += check_unfixable_error(
        "throw.cc", "Don't introduce throws into exception-free files, use error statuses instead.")
    errors += check_unfixable_error("pgv_string.proto", "min_bytes is DEPRECATED, Use min_len.")
    errors += check_file_expecting_ok("commented_throw.cc")
    errors += check_unfixable_error(
        "repository_url.bzl", "Only repository_locations.bzl may contains URL references")
    errors += check_unfixable_error(
        "repository_urls.bzl", "Only repository_locations.bzl may contains URL references")

    # The following files have errors that can be automatically fixed.
    errors += check_and_fix_error(
        "over_enthusiastic_spaces.cc", "./over_enthusiastic_spaces.cc:3: over-enthusiastic spaces")
    errors += check_and_fix_error(
        "extra_enthusiastic_spaces.cc",
        "./extra_enthusiastic_spaces.cc:3: over-enthusiastic spaces")
    errors += check_and_fix_error(
        "angle_bracket_include.cc", "envoy includes should not have angle brackets")
    errors += check_and_fix_error("proto_style.cc", "incorrect protobuf type reference")
    errors += check_and_fix_error("long_line.cc", "clang-format check failed")
    errors += check_and_fix_error("header_order.cc", "header_order.py check failed")
    errors += check_and_fix_error(
        "clang_format_on.cc", "./clang_format_on.cc:7: over-enthusiastic spaces")
    # Validate that a missing license is added.
    errors += check_and_fix_error("license.BUILD", "envoy_build_fixer check failed")
    # Validate that an incorrect license is replaced and reordered.
    errors += check_and_fix_error("update_license.BUILD", "envoy_build_fixer check failed")
    # Validate that envoy_package() is added where there is an envoy_* rule occurring.
    errors += check_and_fix_error("add_envoy_package.BUILD", "envoy_build_fixer check failed")
    # Validate that we don't add envoy_package() when no envoy_* rule.
    errors += check_file_expecting_ok("skip_envoy_package.BUILD")
    # Validate that we clean up gratuitous blank lines.
    errors += check_and_fix_error("canonical_spacing.BUILD", "envoy_build_fixer check failed")
    # Validate that unused loads are removed.
    errors += check_and_fix_error("remove_unused_loads.BUILD", "envoy_build_fixer check failed")
    # Validate that API proto package deps are computed automagically.
    errors += check_and_fix_error(
        "canonical_api_deps.BUILD",
        "envoy_build_fixer check failed",
        extra_input_files=[
            "canonical_api_deps.cc", "canonical_api_deps.h", "canonical_api_deps.other.cc"
        ])
    errors += check_and_fix_error("bad_envoy_build_sys_ref.BUILD", "Superfluous '@envoy//' prefix")
    errors += check_and_fix_error("proto_format.proto", "clang-format check failed")
    errors += check_and_fix_error(
        "cpp_std.cc",
        "term absl::make_unique< should be replaced with standard library term std::make_unique<")
    errors += check_and_fix_error(
        "code_conventions.cc", "term .Times(1); should be replaced with preferred term ;")
    errors += check_and_fix_error(
        "code_conventions.cc",
        "term Stats::ScopePtr should be replaced with preferred term Stats::ScopeSharedPtr")

    errors += check_file_expecting_ok("real_time_source_override.cc")
    errors += check_file_expecting_ok("duration_value_zero.cc")
    errors += check_file_expecting_ok("time_system_wait_for.cc")
    errors += check_file_expecting_ok("clang_format_off.cc")
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
        errors = run_checks()

    if errors != 0:
        logging.error("%d FAILURES" % errors)
        exit(1)
    logging.warning("PASS")
