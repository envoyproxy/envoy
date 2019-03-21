licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_quiche",
)

envoy_package()

# Build targets in this package are part of the QUICHE platform implementation,
# are are not to be consumed or referenced directly by other Envoy code. The
# only consumers should be build rules under @com_googlesource_quiche//..., and
# tests. In a monorepo, this would be enforced via visibility attribute, but
# Bazel does not support limiting visibility to specific external dependencies.

# TODO: add build target for quic_platform_impl_lib

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
        "http2_containers_impl.h",
        "http2_estimate_memory_usage_impl.h",
        "http2_export_impl.h",
        "http2_flag_utils_impl.h",
        "http2_macros_impl.h",
        "http2_optional_impl.h",
        "http2_ptr_util_impl.h",
        "http2_string_impl.h",
        "http2_string_piece_impl.h",
    ] + envoy_select_quiche(["http2_string_utils_impl.h"]),
    external_deps = [
        "abseil_base",
        "abseil_optional",
        "abseil_str_format",
    ],
    visibility = ["//visibility:public"],
    deps = envoy_select_quiche([":string_utils_lib"]),
)

envoy_cc_library(
    name = "quic_platform_export_impl_lib",
    hdrs = ["quic_export_impl.h"],
    visibility = ["//visibility:public"],
)

envoy_cc_library(
    name = "quic_platform_base_impl_lib",
    srcs = envoy_select_quiche(["quic_logging_impl.cc"]),
    hdrs = [
        "quic_aligned_impl.h",
        "quic_arraysize_impl.h",
        "quic_client_stats_impl.h",
        "quic_containers_impl.h",
        "quic_endian_impl.h",
        "quic_estimate_memory_usage_impl.h",
        "quic_fallthrough_impl.h",
        "quic_flag_utils_impl.h",
        "quic_iovec_impl.h",
        "quic_map_util_impl.h",
        "quic_prefetch_impl.h",
        "quic_reference_counted_impl.h",
        "quic_ptr_util_impl.h",
        "quic_server_stats_impl.h",
        "quic_string_piece_impl.h",
        "quic_uint128_impl.h",
    ] + envoy_select_quiche([
        "quic_logging_impl.h",
        "quic_mock_log_impl.h",
        "quic_stack_trace_impl.h",
        "quic_test_impl.h",
        "quic_thread_impl.h",
    ]),
    external_deps = [
        "abseil_base",
        "abseil_hash",
        "abseil_inlined_vector",
        "abseil_memory",
        "abseil_node_hash_map",
        "abseil_node_hash_set",
        "googletest",
    ],
    visibility = ["//visibility:public"],
    deps = ["@com_googlesource_quiche//:quic_platform_export"] + envoy_select_quiche([
        "//include/envoy/thread:thread_interface",
        "//source/common/common:assert_lib",
        "//source/server:backtrace_lib",
    ]),
)

envoy_cc_library(
    name = "quic_platform_impl_lib",
    srcs = ["quic_cert_utils_impl.cc"] + envoy_select_quiche([
        "quic_hostname_utils_impl.cc",
        "quic_test_output_impl.cc",
    ]),
    hdrs = [
        "quic_cert_utils_impl.h",
        "quic_mutex_impl.h",
        "quic_str_cat_impl.h",
    ] + envoy_select_quiche([
        "quic_bug_tracker_impl.h",
        "quic_hostname_utils_impl.h",
        "quic_string_utils_impl.h",
        "quic_test_output_impl.h",
        "quic_text_utils_impl.h",
    ]),
    external_deps = [
        "quiche_quic_platform_base",
        "abseil_str_format",
        "abseil_synchronization",
        "abseil_time",
        "ssl",
    ],
    visibility = ["//visibility:public"],
    deps = envoy_select_quiche([
        ":string_utils_lib",
        "//source/common/filesystem:filesystem_lib",
        "//source/common/http:utility_lib",
    ]),
)

envoy_cc_library(
    name = "quic_platform_sleep_impl_lib",
    hdrs = ["quic_sleep_impl.h"],
    visibility = ["//visibility:public"],
    deps = ["@com_googlesource_quiche//:quic_time_lib"],
)

envoy_cc_library(
    name = "spdy_platform_impl_lib",
    hdrs = [
        "spdy_arraysize_impl.h",
        "spdy_containers_impl.h",
        "spdy_endianness_util_impl.h",
        "spdy_estimate_memory_usage_impl.h",
        "spdy_export_impl.h",
        "spdy_macros_impl.h",
        "spdy_mem_slice_impl.h",
        "spdy_ptr_util_impl.h",
        "spdy_string_impl.h",
        "spdy_string_piece_impl.h",
        "spdy_test_helpers_impl.h",
        "spdy_test_utils_prod_impl.h",
        "spdy_unsafe_arena_impl.h",
    ] + envoy_select_quiche(["spdy_string_utils_impl.h"]),
    external_deps = [
        "abseil_base",
        "abseil_hash",
        "abseil_inlined_vector",
        "abseil_memory",
        "abseil_str_format",
    ],
    visibility = ["//visibility:public"],
    deps = envoy_select_quiche([
        ":string_utils_lib",
        "//source/common/common:assert_lib",
    ]),
)
