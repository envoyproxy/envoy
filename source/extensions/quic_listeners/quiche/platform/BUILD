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

# TODO: add build targets for quic_platform_impl_lib and spdy_platform_impl_lib,
#   as _impl.* files for those are added.

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
        "http2_macros_impl.h",
        "http2_mock_log_impl.h",
        "http2_optional_impl.h",
        "http2_ptr_util_impl.h",
        "http2_reconstruct_object_impl.h",
        "http2_string_impl.h",
        "http2_string_piece_impl.h",
        "http2_string_utils_impl.h",
        "http2_test_helpers_impl.h",
    ],
    external_deps = [
        "abseil_base",
        "abseil_optional",
    ],
    visibility = ["//visibility:public"],
)
