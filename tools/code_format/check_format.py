#!/usr/bin/env python3

import argparse
import common
import functools
import multiprocessing
import os
import os.path
import pathlib
import re
import subprocess
import stat
import sys
import traceback
import shutil
import paths

EXCLUDED_PREFIXES = (
    "./generated/", "./thirdparty/", "./build", "./.git/", "./bazel-", "./.cache",
    "./source/extensions/extensions_build_config.bzl", "./contrib/contrib_build_config.bzl",
    "./bazel/toolchains/configs/", "./tools/testdata/check_format/", "./tools/pyformat/",
    "./third_party/", "./test/extensions/filters/http/wasm/test_data",
    "./test/extensions/filters/network/wasm/test_data",
    "./test/extensions/stats_sinks/wasm/test_data", "./test/extensions/bootstrap/wasm/test_data",
    "./test/extensions/common/wasm/test_data", "./test/extensions/access_loggers/wasm/test_data",
    "./source/extensions/common/wasm/ext", "./examples/wasm-cc", "./bazel/external/http_parser/")
SUFFIXES = ("BUILD", "WORKSPACE", ".bzl", ".cc", ".h", ".java", ".m", ".mm", ".proto")
PROTO_SUFFIX = (".proto")

# Files in these paths can make reference to protobuf stuff directly
GOOGLE_PROTOBUF_ALLOWLIST = (
    "ci/prebuilt", "source/common/protobuf", "api/test", "test/extensions/bootstrap/wasm/test_data")
REPOSITORIES_BZL = "bazel/repositories.bzl"

# Files matching these exact names can reference real-world time. These include the class
# definitions for real-world time, the construction of them in main(), and perf annotation.
# For now it includes the validation server but that really should be injected too.
REAL_TIME_ALLOWLIST = (
    "./source/common/common/utility.h", "./source/extensions/common/aws/utility.cc",
    "./source/common/event/real_time_system.cc", "./source/common/event/real_time_system.h",
    "./source/exe/main_common.cc", "./source/exe/main_common.h",
    "./source/server/config_validation/server.cc", "./source/common/common/perf_annotation.h",
    "./test/common/common/log_macros_test.cc", "./test/common/protobuf/utility_test.cc",
    "./test/test_common/simulated_time_system.cc", "./test/test_common/simulated_time_system.h",
    "./test/test_common/test_time.cc", "./test/test_common/test_time.h",
    "./test/test_common/utility.cc", "./test/test_common/utility.h",
    "./test/integration/integration.h", "./test/tools/wee8_compile/wee8_compile.cc")

# Tests in these paths may make use of the Registry::RegisterFactory constructor or the
# REGISTER_FACTORY macro. Other locations should use the InjectFactory helper class to
# perform temporary registrations.
REGISTER_FACTORY_TEST_ALLOWLIST = (
    "./test/common/config/registry_test.cc", "./test/integration/clusters/",
    "./test/integration/filters/", "./test/integration/load_balancers/",
    "./test/extensions/transport_sockets/tls/integration/")

# Files in these paths can use MessageLite::SerializeAsString
SERIALIZE_AS_STRING_ALLOWLIST = (
    "./source/common/protobuf/utility.cc",
    "./source/extensions/filters/http/grpc_json_transcoder/json_transcoder_filter.cc",
    "./test/common/protobuf/utility_test.cc",
    "./test/common/grpc/codec_test.cc",
    "./test/common/grpc/codec_fuzz_test.cc",
    "./test/extensions/filters/common/expr/context_test.cc",
    "./test/extensions/filters/http/common/fuzz/uber_filter.h",
    "./test/extensions/bootstrap/wasm/test_data/speed_cpp.cc",
    "./test/tools/router_check/router_check.cc",
)

# Files in these paths can use Protobuf::util::JsonStringToMessage
JSON_STRING_TO_MESSAGE_ALLOWLIST = (
    "./source/common/protobuf/utility.cc",
    "./test/extensions/bootstrap/wasm/test_data/speed_cpp.cc")

# Histogram names which are allowed to be suffixed with the unit symbol, all of the pre-existing
# ones were grandfathered as part of PR #8484 for backwards compatibility.
HISTOGRAM_WITH_SI_SUFFIX_ALLOWLIST = (
    "cx_rtt_us", "cx_rtt_variance_us", "downstream_cx_length_ms", "downstream_cx_length_ms",
    "initialization_time_ms", "loop_duration_us", "poll_delay_us", "request_time_ms",
    "upstream_cx_connect_ms", "upstream_cx_length_ms")

# Files in these paths can use std::regex
STD_REGEX_ALLOWLIST = (
    "./source/common/common/utility.cc", "./source/common/common/regex.h",
    "./source/common/common/regex.cc", "./source/common/stats/tag_extractor_impl.h",
    "./source/common/stats/tag_extractor_impl.cc",
    "./source/common/formatter/substitution_formatter.cc",
    "./contrib/squash/filters/http/source/squash_filter.h",
    "./contrib/squash/filters/http/source/squash_filter.cc", "./source/server/admin/utils.h",
    "./source/server/admin/utils.cc", "./source/server/admin/stats_handler.h",
    "./source/server/admin/stats_handler.cc", "./source/server/admin/stats_request.cc",
    "./source/server/admin/stats_request.h", "./source/server/admin/prometheus_stats.h",
    "./source/server/admin/prometheus_stats.cc", "./tools/clang_tools/api_booster/main.cc",
    "./tools/clang_tools/api_booster/proto_cxx_utils.cc", "./source/common/version/version.cc")

# Only one C++ file should instantiate grpc_init
GRPC_INIT_ALLOWLIST = ("./source/common/grpc/google_grpc_context.cc")

# Files that should not raise an error for using memcpy
MEMCPY_WHITELIST = (
    "./source/common/common/mem_block_builder.h", "./source/common/common/safe_memcpy.h")

# These files should not throw exceptions. Add HTTP/1 when exceptions removed.
EXCEPTION_DENYLIST = (
    "./source/common/http/http2/codec_impl.h", "./source/common/http/http2/codec_impl.cc")

# Files that are allowed to use try without main thread assertion.
RAW_TRY_ALLOWLIST = (
    "./source/common/common/regex.cc", "./source/common/common/thread.h",
    "./source/common/network/utility.cc")

# These are entire files that are allowed to use std::string_view vs. individual exclusions. Right
# now this is just WASM which makes use of std::string_view heavily so we need to convert to
# absl::string_view internally. Everywhere else should be using absl::string_view for additional
# safety.
STD_STRING_VIEW_ALLOWLIST = (
    "./source/extensions/common/wasm/context.h",
    "./source/extensions/common/wasm/context.cc",
    "./source/extensions/common/wasm/foreign.cc",
    "./source/extensions/common/wasm/wasm.h",
    "./source/extensions/common/wasm/wasm.cc",
    "./source/extensions/common/wasm/wasm_vm.h",
    "./source/extensions/common/wasm/wasm_vm.cc",
    "./test/extensions/bootstrap/wasm/wasm_speed_test.cc",
    "./test/extensions/bootstrap/wasm/wasm_test.cc",
    "./test/extensions/common/wasm/wasm_test.cc",
    "./test/extensions/stats_sinks/wasm/wasm_stat_sink_test.cc",
    "./test/test_common/wasm_base.h",
)

# Header files that can throw exceptions. These should be limited; the only
# valid situation identified so far is template functions used for config
# processing.
EXCEPTION_ALLOWLIST = ("./source/common/config/utility.h")

# We want all URL references to exist in repository_locations.bzl files and have
# metadata that conforms to the schema in ./api/bazel/external_deps.bzl. Below
# we have some exceptions for either infrastructure files or places we fall
# short today (Rust).
#
# Please DO NOT extend this allow list without consulting
# @envoyproxy/dependency-shepherds.
BUILD_URLS_ALLOWLIST = (
    "./bazel/repository_locations.bzl",
    "./bazel/external/cargo/crates.bzl",
    "./api/bazel/repository_locations.bzl",
    "./api/bazel/envoy_http_archive.bzl",
)

CLANG_FORMAT_PATH = os.getenv("CLANG_FORMAT", "clang-format-12")
BUILDIFIER_PATH = paths.get_buildifier()
BUILDOZER_PATH = paths.get_buildozer()
ENVOY_BUILD_FIXER_PATH = os.path.join(
    os.path.dirname(os.path.abspath(sys.argv[0])), "envoy_build_fixer.py")
HEADER_ORDER_PATH = os.path.join(os.path.dirname(os.path.abspath(sys.argv[0])), "header_order.py")
SUBDIR_SET = set(common.include_dir_order())
INCLUDE_ANGLE = "#include <"
INCLUDE_ANGLE_LEN = len(INCLUDE_ANGLE)
PROTO_PACKAGE_REGEX = re.compile(r"^package (\S+);\n*", re.MULTILINE)
X_ENVOY_USED_DIRECTLY_REGEX = re.compile(r'.*\"x-envoy-.*\".*')
DESIGNATED_INITIALIZER_REGEX = re.compile(r"\{\s*\.\w+\s*\=")
MANGLED_PROTOBUF_NAME_REGEX = re.compile(r"envoy::[a-z0-9_:]+::[A-Z][a-z]\w*_\w*_[A-Z]{2}")
HISTOGRAM_SI_SUFFIX_REGEX = re.compile(r"(?<=HISTOGRAM\()[a-zA-Z0-9_]+_(b|kb|mb|ns|us|ms|s)(?=,)")
TEST_NAME_STARTING_LOWER_CASE_REGEX = re.compile(r"TEST(_.\(.*,\s|\()[a-z].*\)\s\{")
EXTENSIONS_CODEOWNERS_REGEX = re.compile(r'.*(extensions[^@]*\s+)(@.*)')
CONTRIB_CODEOWNERS_REGEX = re.compile(r'(/contrib/[^@]*\s+)(@.*)')
COMMENT_REGEX = re.compile(r"//|\*")
DURATION_VALUE_REGEX = re.compile(r'\b[Dd]uration\(([0-9.]+)')
PROTO_VALIDATION_STRING = re.compile(r'\bmin_bytes\b')
OLD_MOCK_METHOD_REGEX = re.compile("MOCK_METHOD\d")
# C++17 feature, lacks sufficient support across various libraries / compilers.
FOR_EACH_N_REGEX = re.compile("for_each_n\(")
# Check for punctuation in a terminal ref clause, e.g.
# :ref:`panic mode. <arch_overview_load_balancing_panic_threshold>`
DOT_MULTI_SPACE_REGEX = re.compile("\\. +")
FLAG_REGEX = re.compile("RUNTIME_GUARD\((.*)\);")

# yapf: disable
PROTOBUF_TYPE_ERRORS = {
    # Well-known types should be referenced from the ProtobufWkt namespace.
    "Protobuf::Any":                    "ProtobufWkt::Any",
    "Protobuf::Empty":                  "ProtobufWkt::Empty",
    "Protobuf::ListValue":              "ProtobufWkt::ListValue",
    "Protobuf::NULL_VALUE":             "ProtobufWkt::NULL_VALUE",
    "Protobuf::StringValue":            "ProtobufWkt::StringValue",
    "Protobuf::Struct":                 "ProtobufWkt::Struct",
    "Protobuf::Value":                  "ProtobufWkt::Value",

    # Other common mis-namespacing of protobuf types.
    "ProtobufWkt::Map":                 "Protobuf::Map",
    "ProtobufWkt::MapPair":             "Protobuf::MapPair",
    "ProtobufUtil::MessageDifferencer": "Protobuf::util::MessageDifferencer"
}
# yapf: enable

LIBCXX_REPLACEMENTS = {
    "absl::make_unique<": "std::make_unique<",
}

CODE_CONVENTION_REPLACEMENTS = {
    # We can't just remove Times(1) everywhere, since .Times(1).WillRepeatedly
    # is a legitimate pattern. See
    # https://github.com/google/googletest/blob/master/googlemock/docs/for_dummies.md#cardinalities-how-many-times-will-it-be-called
    ".Times(1);": ";",
    # These may miss some cases, due to line breaks, but should reduce the
    # Times(1) noise.
    ".Times(1).WillOnce": ".WillOnce",
    ".Times(1).WillRepeatedly": ".WillOnce",
}

UNSORTED_FLAGS = {
    "envoy.reloadable_features.activate_timers_next_event_loop",
    "envoy.reloadable_features.grpc_json_transcoder_adhere_to_buffer_limits",
    "envoy.reloadable_features.sanitize_http_header_referer",
}


class FormatChecker:

    def __init__(self, args):
        self.operation_type = args.operation_type
        self.target_path = args.target_path
        self.api_prefix = args.api_prefix
        self.envoy_build_rule_check = not args.skip_envoy_build_rule_check
        self.namespace_check = args.namespace_check
        self.namespace_check_excluded_paths = args.namespace_check_excluded_paths + [
            "./tools/api_boost/testdata/",
            "./tools/clang_tools/",
        ]
        self.build_fixer_check_excluded_paths = args.build_fixer_check_excluded_paths + [
            "./bazel/external/",
            "./bazel/toolchains/",
            "./bazel/BUILD",
            "./tools/clang_tools",
        ]
        self.include_dir_order = args.include_dir_order

    # Map a line transformation function across each line of a file,
    # writing the result lines as requested.
    # If there is a clang format nesting or mismatch error, return the first occurrence
    def evaluate_lines(self, path, line_xform, write=True):
        error_message = None
        format_flag = True
        output_lines = []
        for line_number, line in enumerate(self.read_lines(path)):
            if line.find("// clang-format off") != -1:
                if not format_flag and error_message is None:
                    error_message = "%s:%d: %s" % (path, line_number + 1, "clang-format nested off")
                format_flag = False
            if line.find("// clang-format on") != -1:
                if format_flag and error_message is None:
                    error_message = "%s:%d: %s" % (path, line_number + 1, "clang-format nested on")
                format_flag = True
            if format_flag:
                output_lines.append(line_xform(line, line_number))
            else:
                output_lines.append(line)
        # We used to use fileinput in the older Python 2.7 script, but this doesn't do
        # inplace mode and UTF-8 in Python 3, so doing it the manual way.
        if write:
            pathlib.Path(path).write_text('\n'.join(output_lines), encoding='utf-8')
        if not format_flag and error_message is None:
            error_message = "%s:%d: %s" % (path, line_number + 1, "clang-format remains off")
        return error_message

    # Obtain all the lines in a given file.
    def read_lines(self, path):
        return self.read_file(path).split('\n')

    # Read a UTF-8 encoded file as a str.
    def read_file(self, path):
        return pathlib.Path(path).read_text(encoding='utf-8')

    # look_path searches for the given executable in all directories in PATH
    # environment variable. If it cannot be found, empty string is returned.
    def look_path(self, executable):
        if executable is None:
            return ''
        return shutil.which(executable) or ''

    # path_exists checks whether the given path exists. This function assumes that
    # the path is absolute and evaluates environment variables.
    def path_exists(self, executable):
        if executable is None:
            return False
        return os.path.exists(os.path.expandvars(executable))

    # executable_by_others checks whether the given path has execute permission for
    # others.
    def executable_by_others(self, executable):
        st = os.stat(os.path.expandvars(executable))
        return bool(st.st_mode & stat.S_IXOTH)

    # Check whether all needed external tools (clang-format, buildifier, buildozer) are
    # available.
    def check_tools(self):
        error_messages = []

        clang_format_abs_path = self.look_path(CLANG_FORMAT_PATH)
        if clang_format_abs_path:
            if not self.executable_by_others(clang_format_abs_path):
                error_messages.append(
                    "command {} exists, but cannot be executed by other "
                    "users".format(CLANG_FORMAT_PATH))
        else:
            error_messages.append(
                "Command {} not found. If you have clang-format in version 12.x.x "
                "installed, but the binary name is different or it's not available in "
                "PATH, please use CLANG_FORMAT environment variable to specify the path. "
                "Examples:\n"
                "    export CLANG_FORMAT=clang-format-12.0.1\n"
                "    export CLANG_FORMAT=/opt/bin/clang-format-12\n"
                "    export CLANG_FORMAT=/usr/local/opt/llvm@12/bin/clang-format".format(
                    CLANG_FORMAT_PATH))

        def check_bazel_tool(name, path, var):
            bazel_tool_abs_path = self.look_path(path)
            if bazel_tool_abs_path:
                if not self.executable_by_others(bazel_tool_abs_path):
                    error_messages.append(
                        "command {} exists, but cannot be executed by other "
                        "users".format(path))
            elif self.path_exists(path):
                if not self.executable_by_others(path):
                    error_messages.append(
                        "command {} exists, but cannot be executed by other "
                        "users".format(path))
            else:

                error_messages.append(
                    "Command {} not found. If you have {} installed, but the binary "
                    "name is different or it's not available in $GOPATH/bin, please use "
                    "{} environment variable to specify the path. Example:\n"
                    "    export {}=`which {}`\n"
                    "If you don't have {} installed, you can install it by:\n"
                    "    go get -u github.com/bazelbuild/buildtools/{}".format(
                        path, name, var, var, name, name, name))

        check_bazel_tool('buildifier', BUILDIFIER_PATH, 'BUILDIFIER_BIN')
        check_bazel_tool('buildozer', BUILDOZER_PATH, 'BUILDOZER_BIN')

        return error_messages

    def check_namespace(self, file_path):
        for excluded_path in self.namespace_check_excluded_paths:
            if file_path.startswith(excluded_path):
                return []

        nolint = "NOLINT(namespace-%s)" % self.namespace_check.lower()
        text = self.read_file(file_path)
        if not re.search("^\s*namespace\s+%s\s*{" % self.namespace_check, text, re.MULTILINE) and \
                not nolint in text:
            return [
                "Unable to find %s namespace or %s for file: %s" %
                (self.namespace_check, nolint, file_path)
            ]
        return []

    def package_name_for_proto(self, file_path):
        package_name = None
        error_message = []
        result = PROTO_PACKAGE_REGEX.search(self.read_file(file_path))
        if result is not None and len(result.groups()) == 1:
            package_name = result.group(1)
        if package_name is None:
            error_message = ["Unable to find package name for proto file: %s" % file_path]

        return [package_name, error_message]

    # To avoid breaking the Lyft import, we just check for path inclusion here.
    def allow_listed_for_protobuf_deps(self, file_path):
        return (
            file_path.endswith(PROTO_SUFFIX) or file_path.endswith(REPOSITORIES_BZL)
            or any(path_segment in file_path for path_segment in GOOGLE_PROTOBUF_ALLOWLIST))

    # Real-world time sources should not be instantiated in the source, except for a few
    # specific cases. They should be passed down from where they are instantied to where
    # they need to be used, e.g. through the ServerInstance, Dispatcher, or ClusterManager.
    def allow_listed_for_realtime(self, file_path):
        if file_path.endswith(".md"):
            return True
        return file_path in REAL_TIME_ALLOWLIST

    def allow_listed_for_register_factory(self, file_path):
        if not file_path.startswith("./test/"):
            return True

        return any(file_path.startswith(prefix) for prefix in REGISTER_FACTORY_TEST_ALLOWLIST)

    def allow_listed_for_serialize_as_string(self, file_path):
        return file_path in SERIALIZE_AS_STRING_ALLOWLIST

    def allow_listed_for_std_string_view(self, file_path):
        return file_path in STD_STRING_VIEW_ALLOWLIST

    def allow_listed_for_json_string_to_message(self, file_path):
        return file_path in JSON_STRING_TO_MESSAGE_ALLOWLIST

    def allow_listed_for_histogram_si_suffix(self, name):
        return name in HISTOGRAM_WITH_SI_SUFFIX_ALLOWLIST

    def allow_listed_for_std_regex(self, file_path):
        return file_path.startswith("./test") or file_path in STD_REGEX_ALLOWLIST

    def allow_listed_for_grpc_init(self, file_path):
        return file_path in GRPC_INIT_ALLOWLIST

    def allow_listed_for_unpack_to(self, file_path):
        return file_path.startswith("./test") or file_path in [
            "./source/common/protobuf/utility.cc", "./source/common/protobuf/utility.h"
        ]

    def allow_listed_for_raw_try(self, file_path):
        # TODO(chaoqin-li1123): Exclude some important extensions from ALLOWLIST.
        return file_path in RAW_TRY_ALLOWLIST or file_path.startswith("./source/extensions")

    def deny_listed_for_exceptions(self, file_path):
        # Returns true when it is a non test header file or the file_path is in DENYLIST or
        # it is under tools/testdata subdirectory.

        return (file_path.endswith('.h') and not file_path.startswith("./test/") and not file_path in EXCEPTION_ALLOWLIST) or file_path in EXCEPTION_DENYLIST \
            or self.is_in_subdir(file_path, 'tools/testdata')

    def allow_listed_for_build_urls(self, file_path):
        return file_path in BUILD_URLS_ALLOWLIST

    def is_api_file(self, file_path):
        return file_path.startswith(self.api_prefix)

    def is_build_file(self, file_path):
        basename = os.path.basename(file_path)
        if basename in {"BUILD", "BUILD.bazel"} or basename.endswith(".BUILD"):
            return True
        return False

    def is_external_build_file(self, file_path):
        return self.is_build_file(file_path) and (
            file_path.startswith("./bazel/external/")
            or file_path.startswith("./tools/clang_tools"))

    def is_starlark_file(self, file_path):
        return file_path.endswith(".bzl")

    def is_workspace_file(self, file_path):
        return os.path.basename(file_path) == "WORKSPACE"

    def is_build_fixer_excluded_file(self, file_path):
        for excluded_path in self.build_fixer_check_excluded_paths:
            if file_path.startswith(excluded_path):
                return True
        return False

    def has_invalid_angle_bracket_directory(self, line):
        if not line.startswith(INCLUDE_ANGLE):
            return False
        path = line[INCLUDE_ANGLE_LEN:]
        slash = path.find("/")
        if slash == -1:
            return False
        subdir = path[0:slash]
        return subdir in SUBDIR_SET

    # simple check that all flags are sorted.
    def check_runtime_flags(self, file_path, error_messages):
        previous_flag = ""
        for line_number, line in enumerate(self.read_lines(file_path)):
            if line.startswith("RUNTIME_GUARD"):
                match = FLAG_REGEX.match(line)
                if not match:
                    error_messages.append("%s does not look like a reloadable flag" % line)
                    break

                if previous_flag:
                    if line < previous_flag and match.groups()[0] not in UNSORTED_FLAGS:
                        error_messages.append(
                            "%s and %s are out of order\n" % (line, previous_flag))
                previous_flag = line

    def check_file_contents(self, file_path, checker):
        error_messages = []
        if file_path.endswith("source/common/runtime/runtime_features.cc"):
            # Do runtime alphabetical order checks.
            self.check_runtime_flags(file_path, error_messages)

        def check_format_errors(line, line_number):

            def report_error(message):
                error_messages.append("%s:%d: %s" % (file_path, line_number + 1, message))

            checker(line, file_path, report_error)

        evaluate_failure = self.evaluate_lines(file_path, check_format_errors, False)
        if evaluate_failure is not None:
            error_messages.append(evaluate_failure)

        return error_messages

    def fix_source_line(self, line, line_number):
        # Strip double space after '.'  This may prove overenthusiastic and need to
        # be restricted to comments and metadata files but works for now.
        line = re.sub(DOT_MULTI_SPACE_REGEX, ". ", line)

        if self.has_invalid_angle_bracket_directory(line):
            line = line.replace("<", '"').replace(">", '"')

        # Fix incorrect protobuf namespace references.
        for invalid_construct, valid_construct in PROTOBUF_TYPE_ERRORS.items():
            line = line.replace(invalid_construct, valid_construct)

        # Use recommended cpp stdlib
        for invalid_construct, valid_construct in LIBCXX_REPLACEMENTS.items():
            line = line.replace(invalid_construct, valid_construct)

        # Fix code conventions violations.
        for invalid_construct, valid_construct in CODE_CONVENTION_REPLACEMENTS.items():
            line = line.replace(invalid_construct, valid_construct)

        return line

    # We want to look for a call to condvar.waitFor, but there's no strong pattern
    # to the variable name of the condvar. If we just look for ".waitFor" we'll also
    # pick up time_system_.waitFor(...), and we don't want to return true for that
    # pattern. But in that case there is a strong pattern of using time_system in
    # various spellings as the variable name.
    def has_cond_var_wait_for(self, line):
        wait_for = line.find(".waitFor(")
        if wait_for == -1:
            return False
        preceding = line[0:wait_for]
        if preceding.endswith("time_system") or preceding.endswith("timeSystem()") or \
                preceding.endswith("time_system_"):
            return False
        return True

    # Determines whether the filename is either in the specified subdirectory, or
    # at the top level. We consider files in the top level for the benefit of
    # the check_format testcases in tools/testdata/check_format.
    def is_in_subdir(self, filename, *subdirs):
        # Skip this check for check_format's unit-tests.
        if filename.count("/") <= 1:
            return True
        for subdir in subdirs:
            if filename.startswith('./' + subdir + '/'):
                return True
        return False

    # Determines if given token exists in line without leading or trailing token characters
    # e.g. will return True for a line containing foo() but not foo_bar() or baz_foo
    def token_in_line(self, token, line):
        index = 0
        while True:
            index = line.find(token, index)
            # the following check has been changed from index < 1 to index < 0 because
            # this function incorrectly returns false when the token in question is the
            # first one in a line. The following line returns false when the token is present:
            # (no leading whitespace) violating_symbol foo;
            if index < 0:
                break
            if index == 0 or not (line[index - 1].isalnum() or line[index - 1] == '_'):
                if index + len(token) >= len(line) or not (line[index + len(token)].isalnum()
                                                           or line[index + len(token)] == '_'):
                    return True
            index = index + 1
        return False

    def check_source_line(self, line, file_path, report_error):
        # Check fixable errors. These may have been fixed already.
        if line.find(".  ") != -1:
            report_error("over-enthusiastic spaces")
        if self.is_in_subdir(file_path, 'source',
                             'include') and X_ENVOY_USED_DIRECTLY_REGEX.match(line):
            report_error(
                "Please do not use the raw literal x-envoy in source code.  See Envoy::Http::PrefixValue."
            )
        if self.has_invalid_angle_bracket_directory(line):
            report_error("envoy includes should not have angle brackets")
        for invalid_construct, valid_construct in PROTOBUF_TYPE_ERRORS.items():
            if invalid_construct in line:
                report_error(
                    "incorrect protobuf type reference %s; "
                    "should be %s" % (invalid_construct, valid_construct))
        for invalid_construct, valid_construct in LIBCXX_REPLACEMENTS.items():
            if invalid_construct in line:
                report_error(
                    "term %s should be replaced with standard library term %s" %
                    (invalid_construct, valid_construct))
        for invalid_construct, valid_construct in CODE_CONVENTION_REPLACEMENTS.items():
            if invalid_construct in line:
                report_error(
                    "term %s should be replaced with preferred term %s" %
                    (invalid_construct, valid_construct))
        # Do not include the virtual_includes headers.
        if re.search("#include.*/_virtual_includes/", line):
            report_error("Don't include the virtual includes headers.")

        # Some errors cannot be fixed automatically, and actionable, consistent,
        # navigable messages should be emitted to make it easy to find and fix
        # the errors by hand.
        if not self.allow_listed_for_protobuf_deps(file_path):
            if '"google/protobuf' in line or "google::protobuf" in line:
                report_error(
                    "unexpected direct dependency on google.protobuf, use "
                    "the definitions in common/protobuf/protobuf.h instead.")
        if line.startswith("#include <mutex>") or line.startswith("#include <condition_variable"):
            # We don't check here for std::mutex because that may legitimately show up in
            # comments, for example this one.
            report_error(
                "Don't use <mutex> or <condition_variable*>, switch to "
                "Thread::MutexBasicLockable in source/common/common/thread.h")
        if line.startswith("#include <shared_mutex>"):
            # We don't check here for std::shared_timed_mutex because that may
            # legitimately show up in comments, for example this one.
            report_error("Don't use <shared_mutex>, use absl::Mutex for reader/writer locks.")
        if not self.allow_listed_for_realtime(
                file_path) and not "NO_CHECK_FORMAT(real_time)" in line:
            if "RealTimeSource" in line or \
              ("RealTimeSystem" in line and not "TestRealTimeSystem" in line) or \
              "std::chrono::system_clock::now" in line or "std::chrono::steady_clock::now" in line or \
              "std::this_thread::sleep_for" in line or " usleep(" in line or "::usleep(" in line:
                report_error(
                    "Don't reference real-world time sources; use TimeSystem::advanceTime(Wait|Async)"
                )
            if self.has_cond_var_wait_for(line):
                report_error(
                    "Don't use CondVar::waitFor(); use TimeSystem::waitFor() instead.  If this "
                    "already is TimeSystem::waitFor(), please name the TimeSystem variable "
                    "time_system or time_system_ so the linter can understand.")
        duration_arg = DURATION_VALUE_REGEX.search(line)
        if duration_arg and duration_arg.group(1) != "0" and duration_arg.group(1) != "0.0":
            # Matching duration(int-const or float-const) other than zero
            report_error(
                "Don't use ambiguous duration(value), use an explicit duration type, e.g. Event::TimeSystem::Milliseconds(value)"
            )
        if not self.allow_listed_for_register_factory(file_path):
            if "Registry::RegisterFactory<" in line or "REGISTER_FACTORY" in line:
                report_error(
                    "Don't use Registry::RegisterFactory or REGISTER_FACTORY in tests, "
                    "use Registry::InjectFactory instead.")
        if not self.allow_listed_for_unpack_to(file_path):
            if "UnpackTo" in line:
                report_error("Don't use UnpackTo() directly, use MessageUtil::unpackTo() instead")
        # Check that we use the absl::Time library
        if self.token_in_line("std::get_time", line):
            if "test/" in file_path:
                report_error("Don't use std::get_time; use TestUtility::parseTime in tests")
            else:
                report_error("Don't use std::get_time; use the injectable time system")
        if self.token_in_line("std::put_time", line):
            report_error("Don't use std::put_time; use absl::Time equivalent instead")
        if self.token_in_line("gmtime", line):
            report_error("Don't use gmtime; use absl::Time equivalent instead")
        if self.token_in_line("mktime", line):
            report_error("Don't use mktime; use absl::Time equivalent instead")
        if self.token_in_line("localtime", line):
            report_error("Don't use localtime; use absl::Time equivalent instead")
        if self.token_in_line("strftime", line):
            report_error("Don't use strftime; use absl::FormatTime instead")
        if self.token_in_line("strptime", line):
            report_error("Don't use strptime; use absl::FormatTime instead")
        if self.token_in_line("strerror", line):
            report_error("Don't use strerror; use Envoy::errorDetails instead")
        # Prefer using abseil hash maps/sets over std::unordered_map/set for performance optimizations and
        # non-deterministic iteration order that exposes faulty assertions.
        # See: https://abseil.io/docs/cpp/guides/container#hash-tables
        if "std::unordered_map" in line:
            report_error(
                "Don't use std::unordered_map; use absl::flat_hash_map instead or "
                "absl::node_hash_map if pointer stability of keys/values is required")
        if "std::unordered_set" in line:
            report_error(
                "Don't use std::unordered_set; use absl::flat_hash_set instead or "
                "absl::node_hash_set if pointer stability of keys/values is required")
        if "std::atomic_" in line:
            # The std::atomic_* free functions are functionally equivalent to calling
            # operations on std::atomic<T> objects, so prefer to use that instead.
            report_error(
                "Don't use free std::atomic_* functions, use std::atomic<T> members instead.")
        # Block usage of certain std types/functions as iOS 11 and macOS 10.13
        # do not support these at runtime.
        # See: https://github.com/envoyproxy/envoy/issues/12341
        if self.token_in_line("std::any", line):
            report_error("Don't use std::any; use absl::any instead")
        if self.token_in_line("std::get_if", line):
            report_error("Don't use std::get_if; use absl::get_if instead")
        if self.token_in_line("std::holds_alternative", line):
            report_error("Don't use std::holds_alternative; use absl::holds_alternative instead")
        if self.token_in_line("std::make_optional", line):
            report_error("Don't use std::make_optional; use absl::make_optional instead")
        if self.token_in_line("std::monostate", line):
            report_error("Don't use std::monostate; use absl::monostate instead")
        if self.token_in_line("std::optional", line):
            report_error("Don't use std::optional; use absl::optional instead")
        if not self.allow_listed_for_std_string_view(
                file_path) and not "NOLINT(std::string_view)" in line:
            if self.token_in_line("std::string_view", line) or self.token_in_line("toStdStringView",
                                                                                  line):
                report_error(
                    "Don't use std::string_view or toStdStringView; use absl::string_view instead")
        if self.token_in_line("std::variant", line):
            report_error("Don't use std::variant; use absl::variant instead")
        if self.token_in_line("std::visit", line):
            report_error("Don't use std::visit; use absl::visit instead")
        if " try {" in line and file_path.startswith(
                "./source") and not self.allow_listed_for_raw_try(file_path):
            report_error(
                "Don't use raw try, use TRY_ASSERT_MAIN_THREAD if on the main thread otherwise don't use exceptions."
            )
        if "__attribute__((packed))" in line and file_path != "./envoy/common/platform.h":
            # __attribute__((packed)) is not supported by MSVC, we have a PACKED_STRUCT macro that
            # can be used instead
            report_error(
                "Don't use __attribute__((packed)), use the PACKED_STRUCT macro defined "
                "in envoy/common/platform.h instead")
        if DESIGNATED_INITIALIZER_REGEX.search(line):
            # Designated initializers are not part of the C++14 standard and are not supported
            # by MSVC
            report_error(
                "Don't use designated initializers in struct initialization, "
                "they are not part of C++14")
        if " ?: " in line:
            # The ?: operator is non-standard, it is a GCC extension
            report_error("Don't use the '?:' operator, it is a non-standard GCC extension")
        if line.startswith("using testing::Test;"):
            report_error("Don't use 'using testing::Test;, elaborate the type instead")
        if line.startswith("using testing::TestWithParams;"):
            report_error("Don't use 'using testing::Test;, elaborate the type instead")
        if TEST_NAME_STARTING_LOWER_CASE_REGEX.search(line):
            # Matches variants of TEST(), TEST_P(), TEST_F() etc. where the test name begins
            # with a lowercase letter.
            report_error("Test names should be CamelCase, starting with a capital letter")
        if OLD_MOCK_METHOD_REGEX.search(line):
            report_error("The MOCK_METHODn() macros should not be used, use MOCK_METHOD() instead")
        if FOR_EACH_N_REGEX.search(line):
            report_error("std::for_each_n should not be used, use an alternative for loop instead")

        if not self.allow_listed_for_serialize_as_string(file_path) and "SerializeAsString" in line:
            # The MessageLite::SerializeAsString doesn't generate deterministic serialization,
            # use MessageUtil::hash instead.
            report_error(
                "Don't use MessageLite::SerializeAsString for generating deterministic serialization, use MessageUtil::hash instead."
            )
        if not self.allow_listed_for_json_string_to_message(
                file_path) and "JsonStringToMessage" in line:
            # Centralize all usage of JSON parsing so it is easier to make changes in JSON parsing
            # behavior.
            report_error(
                "Don't use Protobuf::util::JsonStringToMessage, use TestUtility::loadFromJson.")

        if self.is_in_subdir(file_path, 'source') and file_path.endswith('.cc') and \
            ('.counterFromString(' in line or '.gaugeFromString(' in line or
             '.histogramFromString(' in line or '.textReadoutFromString(' in line or
             '->counterFromString(' in line or '->gaugeFromString(' in line or
                '->histogramFromString(' in line or '->textReadoutFromString(' in line):
            report_error(
                "Don't lookup stats by name at runtime; use StatName saved during construction")

        if MANGLED_PROTOBUF_NAME_REGEX.search(line):
            report_error("Don't use mangled Protobuf names for enum constants")

        hist_m = HISTOGRAM_SI_SUFFIX_REGEX.search(line)
        if hist_m and not self.allow_listed_for_histogram_si_suffix(hist_m.group(0)):
            report_error(
                "Don't suffix histogram names with the unit symbol, "
                "it's already part of the histogram object and unit-supporting sinks can use this information natively, "
                "other sinks can add the suffix automatically on flush should they prefer to do so."
            )

        normalized_target_path = file_path
        if not normalized_target_path.startswith("./"):
            normalized_target_path = f"./{normalized_target_path}"
        if not self.allow_listed_for_std_regex(normalized_target_path) and "std::regex" in line:
            report_error(
                "Don't use std::regex in code that handles untrusted input. Use RegexMatcher")

        if not self.allow_listed_for_grpc_init(file_path):
            grpc_init_or_shutdown = line.find("grpc_init()")
            grpc_shutdown = line.find("grpc_shutdown()")
            if grpc_init_or_shutdown == -1 or (grpc_shutdown != -1
                                               and grpc_shutdown < grpc_init_or_shutdown):
                grpc_init_or_shutdown = grpc_shutdown
            if grpc_init_or_shutdown != -1:
                comment = line.find("// ")
                if comment == -1 or comment > grpc_init_or_shutdown:
                    report_error(
                        "Don't call grpc_init() or grpc_shutdown() directly, instantiate "
                        + "Grpc::GoogleGrpcContext. See #8282")

        if not self.whitelisted_for_memcpy(file_path) and \
           not ("test/" in file_path) and \
           ("memcpy(" in line) and \
           not ("NOLINT(safe-memcpy)" in line):
            report_error(
                "Don't call memcpy() directly; use safeMemcpy, safeMemcpyUnsafeSrc, safeMemcpyUnsafeDst or MemBlockBuilder instead."
            )

        if self.deny_listed_for_exceptions(file_path):
            # Skpping cases where 'throw' is a substring of a symbol like in "foothrowBar".
            if "throw" in line.split():
                comment_match = COMMENT_REGEX.search(line)
                if comment_match is None or comment_match.start(0) > line.find("throw"):
                    report_error(
                        "Don't introduce throws into exception-free files, use error "
                        + "statuses instead.")

        if "lua_pushlightuserdata" in line:
            report_error(
                "Don't use lua_pushlightuserdata, since it can cause unprotected error in call to"
                + "Lua API (bad light userdata pointer) on ARM64 architecture. See "
                + "https://github.com/LuaJIT/LuaJIT/issues/450#issuecomment-433659873 for details.")

        if file_path.endswith(PROTO_SUFFIX):
            exclude_path = ['v1', 'v2']
            result = PROTO_VALIDATION_STRING.search(line)
            if result is not None:
                if not any(x in file_path for x in exclude_path):
                    report_error("min_bytes is DEPRECATED, Use min_len.")

    def check_build_line(self, line, file_path, report_error):
        if "@bazel_tools" in line and not (self.is_starlark_file(file_path)
                                           or file_path.startswith("./bazel/")
                                           or "python/runfiles" in line):
            report_error(
                "unexpected @bazel_tools reference, please indirect via a definition in //bazel")
        if not self.allow_listed_for_protobuf_deps(file_path) and '"protobuf"' in line:
            report_error(
                "unexpected direct external dependency on protobuf, use "
                "//source/common/protobuf instead.")
        if (self.envoy_build_rule_check and not self.is_starlark_file(file_path)
                and not self.is_workspace_file(file_path)
                and not self.is_external_build_file(file_path) and "@envoy//" in line):
            report_error("Superfluous '@envoy//' prefix")
        if not self.allow_listed_for_build_urls(file_path) and (" urls = " in line
                                                                or " url = " in line):
            report_error("Only repository_locations.bzl may contains URL references")

    def fix_build_line(self, file_path, line, line_number):
        if (self.envoy_build_rule_check and not self.is_starlark_file(file_path)
                and not self.is_workspace_file(file_path)
                and not self.is_external_build_file(file_path)):
            line = line.replace("@envoy//", "//")
        return line

    def fix_build_path(self, file_path):
        self.evaluate_lines(file_path, functools.partial(self.fix_build_line, file_path))

        error_messages = []

        # TODO(htuch): Add API specific BUILD fixer script.
        if not self.is_build_fixer_excluded_file(file_path) and not self.is_api_file(
                file_path) and not self.is_starlark_file(file_path) and not self.is_workspace_file(
                    file_path):
            if os.system("%s %s %s" % (ENVOY_BUILD_FIXER_PATH, file_path, file_path)) != 0:
                error_messages += ["envoy_build_fixer rewrite failed for file: %s" % file_path]

        if os.system("%s -lint=fix -mode=fix %s" % (BUILDIFIER_PATH, file_path)) != 0:
            error_messages += ["buildifier rewrite failed for file: %s" % file_path]
        return error_messages

    def check_build_path(self, file_path):
        error_messages = []

        if not self.is_build_fixer_excluded_file(file_path) and not self.is_api_file(
                file_path) and not self.is_starlark_file(file_path) and not self.is_workspace_file(
                    file_path):
            command = "%s %s | diff %s -" % (ENVOY_BUILD_FIXER_PATH, file_path, file_path)
            error_messages += self.execute_command(
                command, "envoy_build_fixer check failed", file_path)

        if self.is_build_file(file_path) and file_path.startswith(self.api_prefix + "envoy"):
            found = False
            for line in self.read_lines(file_path):
                if "api_proto_package(" in line:
                    found = True
                    break
            if not found:
                error_messages += ["API build file does not provide api_proto_package()"]

        command = "%s -mode=diff %s" % (BUILDIFIER_PATH, file_path)
        error_messages += self.execute_command(command, "buildifier check failed", file_path)
        error_messages += self.check_file_contents(file_path, self.check_build_line)
        return error_messages

    def fix_source_path(self, file_path):
        self.evaluate_lines(file_path, self.fix_source_line)

        error_messages = []

        if not file_path.endswith(PROTO_SUFFIX):
            error_messages += self.fix_header_order(file_path)
        error_messages += self.clang_format(file_path)
        if file_path.endswith(PROTO_SUFFIX) and self.is_api_file(file_path):
            package_name, error_message = self.package_name_for_proto(file_path)
            if package_name is None:
                error_messages += error_message
        return error_messages

    def check_source_path(self, file_path):
        error_messages = self.check_file_contents(file_path, self.check_source_line)

        if not file_path.endswith(PROTO_SUFFIX):
            error_messages += self.check_namespace(file_path)
            command = (
                "%s --include_dir_order %s --path %s | diff %s -" %
                (HEADER_ORDER_PATH, self.include_dir_order, file_path, file_path))
            error_messages += self.execute_command(
                command, "header_order.py check failed", file_path)
        command = ("%s %s | diff %s -" % (CLANG_FORMAT_PATH, file_path, file_path))
        error_messages += self.execute_command(command, "clang-format check failed", file_path)

        if file_path.endswith(PROTO_SUFFIX) and self.is_api_file(file_path):
            package_name, error_message = self.package_name_for_proto(file_path)
            if package_name is None:
                error_messages += error_message
        return error_messages

    # Example target outputs are:
    #   - "26,27c26"
    #   - "12,13d13"
    #   - "7a8,9"
    def execute_command(
        self,
        command,
        error_message,
        file_path,
        regex=re.compile(r"^(\d+)[a|c|d]?\d*(?:,\d+[a|c|d]?\d*)?$")):
        try:
            output = subprocess.check_output(command, shell=True, stderr=subprocess.STDOUT).strip()
            if output:
                return output.decode('utf-8').split("\n")
            return []
        except subprocess.CalledProcessError as e:
            if (e.returncode != 0 and e.returncode != 1):
                return ["ERROR: something went wrong while executing: %s" % e.cmd]
            # In case we can't find any line numbers, record an error message first.
            error_messages = ["%s for file: %s" % (error_message, file_path)]
            for line in e.output.decode('utf-8').splitlines():
                for num in regex.findall(line):
                    error_messages.append("  %s:%s" % (file_path, num))
            return error_messages

    def fix_header_order(self, file_path):
        command = "%s --rewrite --include_dir_order %s --path %s" % (
            HEADER_ORDER_PATH, self.include_dir_order, file_path)
        if os.system(command) != 0:
            return ["header_order.py rewrite error: %s" % (file_path)]
        return []

    def clang_format(self, file_path):
        command = "%s -i %s" % (CLANG_FORMAT_PATH, file_path)
        if os.system(command) != 0:
            return ["clang-format rewrite error: %s" % (file_path)]
        return []

    def check_format(self, file_path):
        error_messages = []
        # Apply fixes first, if asked, and then run checks. If we wind up attempting to fix
        # an issue, but there's still an error, that's a problem.
        try_to_fix = self.operation_type == "fix"
        if self.is_build_file(file_path) or self.is_starlark_file(
                file_path) or self.is_workspace_file(file_path):
            if try_to_fix:
                error_messages += self.fix_build_path(file_path)
            error_messages += self.check_build_path(file_path)
        else:
            if try_to_fix:
                error_messages += self.fix_source_path(file_path)
            error_messages += self.check_source_path(file_path)

        if error_messages:
            return ["From %s" % file_path] + error_messages
        return error_messages

    def check_format_return_trace_on_error(self, file_path):
        """Run check_format and return the traceback of any exception."""
        try:
            return self.check_format(file_path)
        except:
            return traceback.format_exc().split("\n")

    def check_owners(self, dir_name, owned_directories, error_messages):
        """Checks to make sure a given directory is present either in CODEOWNERS or OWNED_EXTENSIONS
    Args:
      dir_name: the directory being checked.
      owned_directories: directories currently listed in CODEOWNERS.
      error_messages: where to put an error message for new unowned directories.
    """
        found = False
        for owned in owned_directories:
            if owned.startswith(dir_name) or dir_name.startswith(owned):
                found = True
        if not found:
            error_messages.append(
                "New directory %s appears to not have owners in CODEOWNERS" % dir_name)

    def check_format_visitor(self, arg, dir_name, names):
        """Run check_format in parallel for the given files.
    Args:
      arg: a tuple (pool, result_list, owned_directories, error_messages)
        pool and result_list are for starting tasks asynchronously.
        owned_directories tracks directories listed in the CODEOWNERS file.
        error_messages is a list of string format errors.
      dir_name: the parent directory of the given files.
      names: a list of file names.
    """

        # Unpack the multiprocessing.Pool process pool and list of results. Since
        # python lists are passed as references, this is used to collect the list of
        # async results (futures) from running check_format and passing them back to
        # the caller.
        pool, result_list, owned_directories, error_messages = arg

        # Sanity check CODEOWNERS.  This doesn't need to be done in a multi-threaded
        # manner as it is a small and limited list.
        source_prefix = './source/'
        core_extensions_full_prefix = './source/extensions/'
        # Check to see if this directory is a subdir under /source/extensions
        # Also ignore top level directories under /source/extensions since we don't
        # need owners for source/extensions/access_loggers etc, just the subdirectories.
        if dir_name.startswith(
                core_extensions_full_prefix) and '/' in dir_name[len(core_extensions_full_prefix):]:
            self.check_owners(dir_name[len(source_prefix):], owned_directories, error_messages)

        # For contrib extensions we track ownership at the top level only.
        contrib_prefix = './contrib/'
        if dir_name.startswith(contrib_prefix):
            top_level = pathlib.PurePath('/', *pathlib.PurePath(dir_name).parts[:2], '/')
            self.check_owners(str(top_level), owned_directories, error_messages)

        dir_name = normalize_path(dir_name)

        for file_name in names:
            result = pool.apply_async(
                self.check_format_return_trace_on_error, args=(dir_name + file_name,))
            result_list.append(result)

    # check_error_messages iterates over the list with error messages and prints
    # errors and returns a bool based on whether there were any errors.
    def check_error_messages(self, error_messages):
        if error_messages:
            for e in error_messages:
                print("ERROR: %s" % e)
            return True
        return False

    def whitelisted_for_memcpy(self, file_path):
        return file_path in MEMCPY_WHITELIST


def normalize_path(path):
    """Convert path to form ./path/to/dir/ for directories and ./path/to/file otherwise"""
    if not path.startswith("./"):
        path = "./" + path

    isdir = os.path.isdir(path)
    if isdir and not path.endswith("/"):
        path += "/"

    return path


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Check or fix file format.")
    parser.add_argument(
        "operation_type",
        type=str,
        choices=["check", "fix"],
        help="specify if the run should 'check' or 'fix' format.")
    parser.add_argument(
        "target_path",
        type=str,
        nargs="?",
        default=".",
        help="specify the root directory for the script to recurse over. Default '.'.")
    parser.add_argument(
        "--add-excluded-prefixes", type=str, nargs="+", help="exclude additional prefixes.")
    parser.add_argument(
        "-j",
        "--num-workers",
        type=int,
        default=multiprocessing.cpu_count(),
        help="number of worker processes to use; defaults to one per core.")
    parser.add_argument("--api-prefix", type=str, default="./api/", help="path of the API tree.")
    parser.add_argument(
        "--skip_envoy_build_rule_check",
        action="store_true",
        help="skip checking for '@envoy//' prefix in build rules.")
    parser.add_argument(
        "--namespace_check",
        type=str,
        nargs="?",
        default="Envoy",
        help="specify namespace check string. Default 'Envoy'.")
    parser.add_argument(
        "--namespace_check_excluded_paths",
        type=str,
        nargs="+",
        default=[],
        help="exclude paths from the namespace_check.")
    parser.add_argument(
        "--build_fixer_check_excluded_paths",
        type=str,
        nargs="+",
        default=[],
        help="exclude paths from envoy_build_fixer check.")
    parser.add_argument(
        "--bazel_tools_check_excluded_paths",
        type=str,
        nargs="+",
        default=[],
        help="exclude paths from bazel_tools check.")
    parser.add_argument(
        "--include_dir_order",
        type=str,
        default=",".join(common.include_dir_order()),
        help="specify the header block include directory order.")
    args = parser.parse_args()
    if args.add_excluded_prefixes:
        EXCLUDED_PREFIXES += tuple(args.add_excluded_prefixes)
    format_checker = FormatChecker(args)

    # Check whether all needed external tools are available.
    ct_error_messages = format_checker.check_tools()
    if format_checker.check_error_messages(ct_error_messages):
        sys.exit(1)

    def check_visibility(error_messages):
        # https://github.com/envoyproxy/envoy/issues/20589
        # https://github.com/envoyproxy/envoy/issues/9953
        # PLEASE DO NOT ADD FILES TO THIS LIST WITHOUT SENIOR MAINTAINER APPROVAL
        exclude_list = (
            "':(exclude)source/extensions/filters/http/buffer/BUILD' "
            "':(exclude)source/extensions/filters/network/common/BUILD' ")
        command = (
            "git diff $(tools/git/last_github_commit.sh) -- source/extensions/* %s |grep '+.*visibility ='"
            % exclude_list)
        try:
            output = subprocess.check_output(command, shell=True, stderr=subprocess.STDOUT).strip()
            if output:
                error_messages.append(
                    "This change appears to add visibility rules. Please get senior maintainer "
                    "approval to add an exemption to check_visibility tools/code_format/check_format.py"
                )
            output = subprocess.check_output(
                "grep -r --include BUILD envoy_package source/extensions/*",
                shell=True,
                stderr=subprocess.STDOUT).strip()
            if output:
                error_messages.append(
                    "envoy_package is not allowed to be used in source/extensions BUILD files.")
        except subprocess.CalledProcessError as e:
            if (e.returncode != 0 and e.returncode != 1):
                error_messages.append("Failed to check visibility with command %s" % command)

    def get_owners():
        with open('./OWNERS.md') as f:
            EXTENSIONS_CODEOWNERS_REGEX = re.compile(r'.*github.com.(.*)\)\)')
            maintainers = ["@UNOWNED"]
            for line in f:
                if "Senior extension maintainers" in line:
                    return maintainers
                m = EXTENSIONS_CODEOWNERS_REGEX.search(line)
                if m is not None:
                    maintainers.append("@" + m.group(1).lower())

    # Returns the list of directories with owners listed in CODEOWNERS. May append errors to
    # error_messages.
    def owned_directories(error_messages):
        owned = []
        try:
            maintainers = get_owners()

            with open('./CODEOWNERS') as f:
                for line in f:
                    # If this line is of the form "extensions/... @owner1 @owner2" capture the directory
                    # name and store it in the list of directories with documented owners.
                    m = EXTENSIONS_CODEOWNERS_REGEX.search(line)
                    if m is not None and not line.startswith('#'):
                        owned.append(m.group(1).strip())
                        owners = re.findall('@\S+', m.group(2).strip())
                        if len(owners) < 2:
                            error_messages.append(
                                "Extensions require at least 2 owners in CODEOWNERS:\n"
                                "    {}".format(line))
                        maintainer = len(set(owners).intersection(set(maintainers))) > 0
                        if not maintainer:
                            error_messages.append(
                                "Extensions require at least one maintainer OWNER:\n"
                                "    {}".format(line))

                    m = CONTRIB_CODEOWNERS_REGEX.search(line)
                    if m is not None and not line.startswith('#'):
                        stripped_path = m.group(1).strip()
                        if not stripped_path.endswith('/'):
                            error_messages.append(
                                "Contrib CODEOWNERS entry '{}' must end in '/'".format(
                                    stripped_path))
                            continue

                        if not (stripped_path.count('/') == 3 or
                                (stripped_path.count('/') == 4
                                 and stripped_path.startswith('/contrib/common/'))):
                            error_messages.append(
                                "Contrib CODEOWNERS entry '{}' must be 2 directories deep unless in /contrib/common/ and then it can be 3 directories deep"
                                .format(stripped_path))
                            continue

                        owned.append(stripped_path)
                        owners = re.findall('@\S+', m.group(2).strip())
                        if len(owners) < 2:
                            error_messages.append(
                                "Contrib extensions require at least 2 owners in CODEOWNERS:\n"
                                "    {}".format(line))

            return owned
        except IOError:
            return []  # for the check format tests.

    # Calculate the list of owned directories once per run.
    error_messages = []
    owned_directories = owned_directories(error_messages)

    check_visibility(error_messages)

    if os.path.isfile(args.target_path):
        # All of our EXCLUDED_PREFIXES start with "./", but the provided
        # target path argument might not. Add it here if it is missing,
        # and use that normalized path for both lookup and `check_format`.
        normalized_target_path = normalize_path(args.target_path)
        if not normalized_target_path.startswith(
                EXCLUDED_PREFIXES) and normalized_target_path.endswith(SUFFIXES):
            error_messages += format_checker.check_format(normalized_target_path)
    else:
        results = []

        def pooled_check_format(path_predicate):
            pool = multiprocessing.Pool(processes=args.num_workers)
            # For each file in target_path, start a new task in the pool and collect the
            # results (results is passed by reference, and is used as an output).
            for root, _, files in os.walk(args.target_path):
                _files = []
                for filename in files:
                    file_path = os.path.join(root, filename)
                    check_file = (
                        path_predicate(filename) and not file_path.startswith(EXCLUDED_PREFIXES)
                        and file_path.endswith(SUFFIXES))
                    if check_file:
                        _files.append(filename)
                if not _files:
                    continue
                format_checker.check_format_visitor(
                    (pool, results, owned_directories, error_messages), root, _files)

            # Close the pool to new tasks, wait for all of the running tasks to finish,
            # then collect the error messages.
            pool.close()
            pool.join()

        # We first run formatting on non-BUILD files, since the BUILD file format
        # requires analysis of srcs/hdrs in the BUILD file, and we don't want these
        # to be rewritten by other multiprocessing pooled processes.
        pooled_check_format(lambda f: not format_checker.is_build_file(f))
        pooled_check_format(lambda f: format_checker.is_build_file(f))

        error_messages += sum((r.get() for r in results), [])

    if format_checker.check_error_messages(error_messages):
        print("ERROR: check format failed. run 'tools/code_format/check_format.py fix'")
        sys.exit(1)

    if args.operation_type == "check":
        print("PASS")
