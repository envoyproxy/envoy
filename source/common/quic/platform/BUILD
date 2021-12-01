load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
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
    name = "quiche_flags_impl_lib",
    srcs = ["quiche_flags_impl.cc"],
    hdrs = ["quiche_flags_impl.h"],
    external_deps = [
        "abseil_base",
        "abseil_synchronization",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//source/common/common:assert_lib",
        "@com_github_google_quiche//:quic_core_flags_list_lib",
        "@com_github_google_quiche//:quic_core_protocol_flags_list_lib",
    ],
)

envoy_cc_library(
    name = "http2_platform_impl_lib",
    hdrs = [
        "http2_macros_impl.h",
    ],
    external_deps = [
        "abseil_base",
        "abseil_str_format",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_platform_logging_impl_lib",
        ":quiche_flags_impl_lib",
    ],
)

envoy_cc_library(
    name = "quic_platform_logging_impl_lib",
    srcs = [
        "quic_logging_impl.cc",
        "quiche_bug_tracker_impl.cc",
    ],
    hdrs = [
        "quic_logging_impl.h",
        "quiche_bug_tracker_impl.h",
    ],
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:stl_helpers",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "quic_platform_base_impl_lib",
    srcs = [
        "quic_mem_slice_impl.cc",
    ],
    hdrs = [
        "quic_client_stats_impl.h",
        "quic_error_code_wrappers_impl.h",
        "quic_flags_impl.h",
        "quic_iovec_impl.h",
        "quic_mem_slice_impl.h",
        "quic_reference_counted_impl.h",
        "quic_server_stats_impl.h",
        "quic_stack_trace_impl.h",
        "quic_stream_buffer_allocator_impl.h",
        "quic_testvalue_impl.h",
    ],
    external_deps = [
        "abseil_base",
        "abseil_hash",
        "abseil_inlined_vector",
        "abseil_memory",
        "abseil_node_hash_map",
        "abseil_node_hash_set",
    ],
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_flags_impl_lib",
        "//envoy/api:io_error_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/server:backtrace_lib",
        "@com_github_google_quiche//:quic_core_buffer_allocator_lib",
        "@com_github_google_quiche//:quic_platform_export",
        "@com_github_google_quiche//:quic_platform_ip_address_family",
        "@com_github_google_quiche//:quiche_common_platform",
        "@com_google_absl//absl/container:btree",
    ],
)

envoy_cc_library(
    name = "quic_platform_impl_lib",
    srcs = [
        "quic_hostname_utils_impl.cc",
    ],
    hdrs = [
        "quic_hostname_utils_impl.h",
        "quic_mutex_impl.h",
    ],
    external_deps = [
        "quiche_quic_platform_base",
        "abseil_str_format",
        "abseil_synchronization",
        "abseil_time",
        "ssl",
    ],
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/filesystem:directory_lib",
        "//source/common/filesystem:filesystem_lib",
        "//source/common/http:utility_lib",
    ],
)

envoy_cc_library(
    name = "quic_platform_udp_socket_impl_lib",
    hdrs = select({
        "//bazel:linux": ["quic_udp_socket_platform_impl.h"],
        "//conditions:default": [],
    }),
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
)

envoy_cc_library(
    name = "envoy_quic_clock_lib",
    srcs = ["envoy_quic_clock.cc"],
    hdrs = ["envoy_quic_clock.h"],
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "@com_github_google_quiche//:quic_core_clock_lib",
    ],
)

envoy_cc_library(
    name = "quiche_common_platform_impl_lib",
    hdrs = [
        "quiche_flag_utils_impl.h",
        "quiche_logging_impl.h",
    ],
    external_deps = [
        "abseil_hash",
        "abseil_node_hash_map",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_platform_logging_impl_lib",
        ":quiche_flags_impl_lib",
    ],
)
