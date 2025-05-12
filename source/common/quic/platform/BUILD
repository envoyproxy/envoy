load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)
load(
    "quiche.bzl",
    "envoy_quiche_platform_impl_cc_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

# Build targets in this package are part of the QUICHE platform implementation.
# These implementations are the infrastructure building block for QUIC. They are
# used in 2 different ways:
#
# Most of them are not to be consumed or referenced directly by other Envoy code.
# Their only consumers should be build rules under @com_github_google_quiche//...,
# and tests. In a monorepo, this would be enforced via visibility attribute, but
# Bazel does not support limiting visibility to specific external dependencies.
#
# Very few of them are used by Envoy to interact with QUIC. They are used as a shim
# to match a non-virtualized API required by the external Quiche implementation.
#
# See a detailed description of QUIC platform API dependency model at:
# https://quiche.googlesource.com/quiche/+/refs/heads/master/quic/platform/api/README.md

# These implementations are tested through their APIs with tests mostly brought in from
# QUICHE, thus new unit tests for them are deliberately omitted in Envoy tree. These
# tests are added to @com_github_google_quiche//:quic_platform_api_test. And all tests
# under @com_github_google_quiche// are configured in test/coverage/gen_build.sh to run in
# CI.
# For some APIs which are not covered in QUICHE tests, their tests is added into
# //test/common/quic/platform/.

# TODO: add build target for quic_platform_impl_lib

envoy_cc_library(
    name = "quiche_flags_constants",
    hdrs = ["quiche_flags_constants.h"],
    deps = [
        "//source/common/http:http_option_limits_lib",
        "@com_google_absl//absl/base",
    ],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_flags_impl_lib",
    srcs = ["quiche_flags_impl.cc"],
    hdrs = ["quiche_flags_impl.h"],
    deps = [
        ":quiche_flags_constants",
        "//source/common/common:assert_lib",
        "@com_github_google_quiche//:quiche_feature_flags_list_lib",
        "@com_github_google_quiche//:quiche_protocol_flags_list_lib",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/synchronization",
    ],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_time_utils_impl_lib",
    srcs = ["quiche_time_utils_impl.cc"],
    hdrs = ["quiche_time_utils_impl.h"],
    deps = [
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/time",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_logging_impl_lib",
    srcs = [
        "quiche_bug_tracker_impl.cc",
        "quiche_logging_impl.cc",
    ],
    hdrs = [
        "quiche_bug_tracker_impl.h",
        "quiche_logging_impl.h",
    ],
    tags = ["nofips"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_quiche_platform_impl_cc_library(
    name = "quic_base_impl_lib",
    tags = ["nofips"],
    deps = [
        ":quiche_flags_impl_lib",
        "//source/common/common:assert_lib",
        "@com_github_google_quiche//:quic_platform_export",
        "@com_google_absl//absl/base",
        "@com_google_absl//absl/container:btree",
        "@com_google_absl//absl/container:inlined_vector",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/memory",
    ],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_platform_iovec_impl_lib",
    hdrs = [
        "quiche_iovec_impl.h",
    ],
    tags = ["nofips"],
    deps = [
        "//envoy/common:base_includes",
    ],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_stack_trace_impl_lib",
    hdrs = [
        "quiche_stack_trace_impl.h",
    ],
    tags = ["nofips"],
    deps = [
        "//source/server:backtrace_lib",
        "@com_github_google_quiche//:quiche_common_platform_export",
    ],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_mem_slice_impl_lib",
    srcs = [
        "quiche_mem_slice_impl.cc",
    ],
    hdrs = [
        "quiche_mem_slice_impl.h",
    ],
    deps = [
        ":quiche_flags_impl_lib",
        ":quiche_logging_impl_lib",
        "//source/common/buffer:buffer_lib",
        "@com_github_google_quiche//:quiche_common_buffer_allocator_lib",
        "@com_github_google_quiche//:quiche_common_callbacks",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/hash",
    ],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_lower_case_string_impl_lib",
    hdrs = ["quiche_lower_case_string_impl.h"],
    tags = ["nofips"],
    deps = ["//envoy/http:header_map_interface"],
)

envoy_quiche_platform_impl_cc_library(
    name = "quiche_export_impl_lib",
    hdrs = ["quiche_export_impl.h"],
    tags = ["nofips"],
    deps = ["@com_google_absl//absl/base"],
)
