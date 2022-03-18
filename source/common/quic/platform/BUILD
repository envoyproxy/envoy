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
    include_prefix = "quiche_platform_impl",
    visibility = ["//visibility:public"],
    deps = [
        "//source/common/common:assert_lib",
        "@com_github_google_quiche//:quic_core_flags_list_lib",
        "@com_github_google_quiche//:quic_core_protocol_flags_list_lib",
    ],
)

envoy_cc_library(
    name = "quiche_platform_logging_impl_lib",
    srcs = [
        "quiche_bug_tracker_impl.cc",
        "quiche_logging_impl.cc",
    ],
    hdrs = [
        "quiche_bug_tracker_impl.h",
        "quiche_logging_impl.h",
    ],
    include_prefix = "quiche_platform_impl",
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
    hdrs = [
        "quic_flags_impl.h",
    ],
    external_deps = [
        "abseil_base",
        "abseil_hash",
        "abseil_inlined_vector",
        "abseil_memory",
        "abseil_node_hash_map",
        "abseil_node_hash_set",
    ],
    include_prefix = "quiche_platform_impl",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_flags_impl_lib",
        "//envoy/api:io_error_interface",
        "//source/common/common:assert_lib",
        "//source/server:backtrace_lib",
        "@com_github_google_quiche//:quic_platform_export",
        "@com_github_google_quiche//:quic_platform_ip_address_family",
        "@com_github_google_quiche//:quiche_common_platform",
        "@com_google_absl//absl/container:btree",
    ],
)

envoy_cc_library(
    name = "quiche_platform_iovec_impl_lib",
    hdrs = [
        "quiche_iovec_impl.h",
    ],
    external_deps = [
        "abseil_base",
        "abseil_hash",
        "abseil_inlined_vector",
        "abseil_memory",
        "abseil_node_hash_map",
        "abseil_node_hash_set",
    ],
    include_prefix = "quiche_platform_impl",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
    ],
)

envoy_cc_library(
    name = "quiche_platform_hostname_utils_impl_lib",
    srcs = [
        "quiche_hostname_utils_impl.cc",
    ],
    hdrs = [
        "quiche_hostname_utils_impl.h",
    ],
    external_deps = [
        "abseil_str_format",
        "quiche_quic_platform_base",
    ],
    include_prefix = "quiche_platform_impl",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        "//source/common/http:utility_lib",
        "@com_github_google_quiche//:quiche_common_platform_export",
    ],
)

envoy_cc_library(
    name = "quiche_platform_stream_buffer_allocator_impl_lib",
    hdrs = [
        "quiche_stream_buffer_allocator_impl.h",
    ],
    include_prefix = "quiche_platform_impl",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_github_google_quiche//:quiche_common_buffer_allocator_lib",
    ],
)

envoy_cc_library(
    name = "quiche_platform_error_code_wrappers_impl_lib",
    hdrs = [
        "quiche_error_code_wrappers_impl.h",
    ],
    include_prefix = "quiche_platform_impl",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_github_google_quiche//:quiche_common_platform_export",
    ],
)

envoy_cc_library(
    name = "quiche_platform_stack_trace_impl_lib",
    hdrs = [
        "quiche_stack_trace_impl.h",
    ],
    include_prefix = "quiche_platform_impl",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_github_google_quiche//:quiche_common_platform_export",
    ],
)

envoy_cc_library(
    name = "quiche_platform_server_stats_impl_lib",
    hdrs = [
        "quiche_server_stats_impl.h",
    ],
    include_prefix = "quiche_platform_impl",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_github_google_quiche//:quiche_common_platform_export",
    ],
)

envoy_cc_library(
    name = "quiche_platform_udp_socket_platform_impl_lib",
    hdrs = select({
        "//bazel:linux": ["quiche_udp_socket_platform_impl.h"],
        "//conditions:default": [],
    }),
    include_prefix = "quiche_platform_impl",
    repository = "@envoy",
    tags = ["nofips"],
    visibility = ["//visibility:public"],
)

envoy_cc_library(
    name = "quiche_common_platform_mem_slice_impl_lib",
    srcs = [
        "quiche_mem_slice_impl.cc",
    ],
    hdrs = [
        "quiche_mem_slice_impl.h",
    ],
    external_deps = [
        "abseil_hash",
        "abseil_node_hash_map",
    ],
    include_prefix = "quiche_platform_impl",
    visibility = ["//visibility:public"],
    deps = [
        ":quiche_flags_impl_lib",
        ":quiche_platform_logging_impl_lib",
        "//source/common/buffer:buffer_lib",
        "@com_github_google_quiche//:quiche_common_buffer_allocator_lib",
    ],
)
