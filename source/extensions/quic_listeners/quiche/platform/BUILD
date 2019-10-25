licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

# Build targets in this package are part of the QUICHE platform implementation.
# These implementations are the infrastructure building block for QUIC. They are
# used in 2 different ways:
#
# Most of them are not to be consumed or referenced directly by other Envoy code.
# Their only consumers should be build rules under @com_googlesource_quiche//...,
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
# tests are added to @com_googlesource_quiche//:quic_platform_api_test. And all tests
# under @com_googlesource_quiche// are configured in test/coverage/gen_build.sh to run in
# CI.
# For some APIs which are not covered in QUICHE tests, their tests is added into
# //test/extensions/quic_listeners/quiche/platform/.

# TODO: add build target for quic_platform_impl_lib

envoy_cc_library(
    name = "flags_impl_lib",
    srcs = ["flags_impl.cc"],
    hdrs = [
        "flags_impl.h",
        "flags_list.h",
    ],
    external_deps = [
        "abseil_base",
        "abseil_synchronization",
    ],
    visibility = ["//visibility:public"],
)

envoy_cc_library(
    name = "string_utils_lib",
    srcs = ["string_utils.cc"],
    hdrs = ["string_utils.h"],
    external_deps = ["abseil_str_format"],
    visibility = ["//visibility:private"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:base64_lib",
    ],
)

envoy_cc_library(
    name = "http2_platform_impl_lib",
    hdrs = [
        "http2_arraysize_impl.h",
        "http2_bug_tracker_impl.h",
        "http2_containers_impl.h",
        "http2_estimate_memory_usage_impl.h",
        "http2_export_impl.h",
        "http2_flag_utils_impl.h",
        "http2_flags_impl.h",
        "http2_logging_impl.h",
        "http2_macros_impl.h",
        "http2_optional_impl.h",
        "http2_ptr_util_impl.h",
        "http2_string_impl.h",
        "http2_string_piece_impl.h",
        "http2_string_utils_impl.h",
    ],
    external_deps = [
        "abseil_base",
        "abseil_optional",
        "abseil_str_format",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":flags_impl_lib",
        ":quic_platform_logging_impl_lib",
        ":string_utils_lib",
    ],
)

envoy_cc_library(
    name = "quic_platform_export_impl_lib",
    hdrs = ["quic_export_impl.h"],
    tags = ["nofips"],
    visibility = ["//visibility:public"],
)

envoy_cc_library(
    name = "quic_platform_logging_impl_lib",
    srcs = ["quic_logging_impl.cc"],
    hdrs = [
        "quic_bug_tracker_impl.h",
        "quic_logging_impl.h",
    ],
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:stl_helpers",
    ],
)

envoy_cc_library(
    name = "quic_platform_base_impl_lib",
    srcs = [
        "quic_mem_slice_impl.cc",
    ],
    hdrs = [
        "quic_aligned_impl.h",
        "quic_arraysize_impl.h",
        "quic_client_stats_impl.h",
        "quic_containers_impl.h",
        "quic_endian_impl.h",
        "quic_error_code_wrappers_impl.h",
        "quic_estimate_memory_usage_impl.h",
        "quic_fallthrough_impl.h",
        "quic_flag_utils_impl.h",
        "quic_flags_impl.h",
        "quic_iovec_impl.h",
        "quic_macros_impl.h",
        "quic_map_util_impl.h",
        "quic_mem_slice_impl.h",
        "quic_optional_impl.h",
        "quic_prefetch_impl.h",
        "quic_ptr_util_impl.h",
        "quic_reference_counted_impl.h",
        "quic_server_stats_impl.h",
        "quic_stack_trace_impl.h",
        "quic_str_cat_impl.h",
        "quic_stream_buffer_allocator_impl.h",
        "quic_string_piece_impl.h",
        "quic_text_utils_impl.h",
        "quic_uint128_impl.h",
    ],
    external_deps = [
        "abseil_base",
        "abseil_hash",
        "abseil_inlined_vector",
        "abseil_memory",
        "abseil_node_hash_map",
        "abseil_node_hash_set",
        "abseil_optional",
        "googletest",
    ],
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        ":flags_impl_lib",
        ":string_utils_lib",
        "//include/envoy/api:io_error_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:byte_order_lib",
        "//source/server:backtrace_lib",
        "@com_googlesource_quiche//:quic_core_buffer_allocator_lib",
        "@com_googlesource_quiche//:quic_platform_export",
        "@com_googlesource_quiche//:quic_platform_ip_address_family",
        "@com_googlesource_quiche//:quiche_common_platform",
    ],
)

envoy_cc_library(
    name = "quic_platform_impl_lib",
    srcs = [
        "quic_cert_utils_impl.cc",
        "quic_file_utils_impl.cc",
        "quic_hostname_utils_impl.cc",
    ],
    hdrs = [
        "quic_cert_utils_impl.h",
        "quic_file_utils_impl.h",
        "quic_hostname_utils_impl.h",
        "quic_mutex_impl.h",
        "quic_pcc_sender_impl.h",
        "quic_string_utils_impl.h",
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
    name = "quic_platform_mem_slice_span_impl_lib",
    srcs = ["quic_mem_slice_span_impl.cc"],
    hdrs = ["quic_mem_slice_span_impl.h"],
    copts = select({
        "//bazel:windows_x86_64": [],
        "//conditions:default": ["-Wno-unused-parameter"],
    }),
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//source/common/common:stack_array",
        "@com_googlesource_quiche//:quic_core_types_lib",
        "@com_googlesource_quiche//:quic_platform_base",
    ],
)

envoy_cc_library(
    name = "quic_platform_mem_slice_storage_impl_lib",
    srcs = ["quic_mem_slice_storage_impl.cc"],
    hdrs = ["quic_mem_slice_storage_impl.h"],
    copts = select({
        "//bazel:windows_x86_64": [],
        "//conditions:default": [
            "-Wno-error=invalid-offsetof",
            "-Wno-unused-parameter",
        ],
    }),
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_googlesource_quiche//:quic_core_buffer_allocator_lib",
        "@com_googlesource_quiche//:quic_core_utils_lib",
        "@com_googlesource_quiche//:quic_platform_mem_slice_span",
    ],
)

envoy_cc_library(
    name = "quic_platform_bbr2_sender_impl_lib",
    hdrs = ["quic_bbr2_sender_impl.h"],
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = ["@com_googlesource_quiche//:quic_core_congestion_control_bbr_lib"],
)

envoy_cc_library(
    name = "envoy_quic_clock_lib",
    srcs = ["envoy_quic_clock.cc"],
    hdrs = ["envoy_quic_clock.h"],
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "@com_googlesource_quiche//:quic_platform",
    ],
)

envoy_cc_library(
    name = "quiche_common_platform_impl_lib",
    hdrs = [
        "quiche_logging_impl.h",
        "quiche_ptr_util_impl.h",
        "quiche_unordered_containers_impl.h",
    ],
    external_deps = [
        "abseil_hash",
        "abseil_node_hash_map",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":quic_platform_logging_impl_lib",
    ],
)

envoy_cc_library(
    name = "spdy_platform_impl_lib",
    hdrs = [
        "spdy_arraysize_impl.h",
        "spdy_bug_tracker_impl.h",
        "spdy_containers_impl.h",
        "spdy_endianness_util_impl.h",
        "spdy_estimate_memory_usage_impl.h",
        "spdy_export_impl.h",
        "spdy_flags_impl.h",
        "spdy_logging_impl.h",
        "spdy_macros_impl.h",
        "spdy_map_util_impl.h",
        "spdy_mem_slice_impl.h",
        "spdy_ptr_util_impl.h",
        "spdy_string_impl.h",
        "spdy_string_piece_impl.h",
        "spdy_string_utils_impl.h",
        "spdy_test_helpers_impl.h",
        "spdy_test_utils_prod_impl.h",
    ],
    external_deps = [
        "abseil_base",
        "abseil_hash",
        "abseil_inlined_vector",
        "abseil_memory",
        "abseil_str_format",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":flags_impl_lib",
        ":quic_platform_logging_impl_lib",
        ":string_utils_lib",
        "//source/common/common:assert_lib",
        "@com_googlesource_quiche//:quiche_common_lib",
    ],
)

envoy_cc_library(
    name = "spdy_platform_unsafe_arena_impl_lib",
    hdrs = ["spdy_unsafe_arena_impl.h"],
    visibility = ["//visibility:public"],
    deps = ["@com_googlesource_quiche//:spdy_simple_arena_lib"],
)
