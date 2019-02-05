licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

# Build targets in this package are part of the QUICHE platform implementation,
# are are not to be consumed or referenced directly by other Envoy code. The
# only consumers should be build rules under @com_googlesource_quiche//..., and
# tests. In a monorepo, this would be enforced via visibility attribute, but
# Bazel does not support limiting visibility to specific external dependencies.

# TODO: add build target for quic_platform_impl_lib

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
    ],
    external_deps = [
        "abseil_base",
        "abseil_optional",
    ],
    visibility = ["//visibility:public"],
)

envoy_cc_library(
    name = "quic_platform_base_impl_lib",
    hdrs = [
        "quic_aligned_impl.h",
        "quic_arraysize_impl.h",
        "quic_client_stats_impl.h",
        "quic_containers_impl.h",
        "quic_endian_impl.h",
        "quic_estimate_memory_usage_impl.h",
        "quic_export_impl.h",
        "quic_fallthrough_impl.h",
        "quic_flag_utils_impl.h",
        "quic_iovec_impl.h",
        "quic_prefetch_impl.h",
        "quic_ptr_util_impl.h",
        "quic_string_impl.h",
        "quic_string_piece_impl.h",
        "quic_uint128_impl.h",
    ],
    external_deps = [
        "abseil_base",
        "abseil_hash",
        "abseil_inlined_vector",
        "abseil_memory",
        "abseil_node_hash_map",
        "abseil_node_hash_set",
    ],
    visibility = ["//visibility:public"],
)

envoy_cc_library(
    name = "quic_platform_impl_lib",
    hdrs = [
        "quic_mutex_impl.h",
        "quic_str_cat_impl.h",
        "quic_string_utils_impl.h",
    ],
    external_deps = [
        "quiche_quic_platform_base",
        "abseil_synchronization",
    ],
    visibility = ["//visibility:public"],
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
        "spdy_ptr_util_impl.h",
        "spdy_string_impl.h",
        "spdy_string_piece_impl.h",
        "spdy_test_helpers_impl.h",
        "spdy_test_utils_prod_impl.h",
        "spdy_unsafe_arena_impl.h",
    ],
    external_deps = [
        "abseil_base",
        "abseil_hash",
        "abseil_inlined_vector",
        "abseil_memory",
    ],
    visibility = ["//visibility:public"],
)
