load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "pass_through_filter_lib",
    hdrs = ["pass_through_filter.h"],
    # A thin shim used by test and prod filters.
    visibility = ["//visibility:public"],
    deps = [
        "//include/envoy/server:filter_config_interface",
    ],
)

envoy_cc_library(
    name = "factory_base_lib",
    hdrs = ["factory_base.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//include/envoy/server:filter_config_interface",
    ],
)

envoy_cc_library(
    name = "jwks_fetcher_lib",
    srcs = ["jwks_fetcher.cc"],
    hdrs = ["jwks_fetcher.h"],
    external_deps = [
        "jwt_verify_lib",
    ],
    deps = [
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/http:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    # Used by the router filter.  TODO(#9953) clean up.
    visibility = [
        "//source:__subpackages__",
        "//test:__subpackages__",
    ],
    deps = [
        "//include/envoy/runtime:runtime_interface",
        "//source/common/common:macros",
        "//source/extensions/common:utility_lib",
        "//source/extensions/filters/http:well_known_names",
    ],
)

envoy_cc_library(
    name = "stream_rate_limiter_lib",
    srcs = ["stream_rate_limiter.cc"],
    hdrs = ["stream_rate_limiter.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:token_bucket_impl_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
