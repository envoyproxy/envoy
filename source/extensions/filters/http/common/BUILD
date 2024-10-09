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
        "//envoy/server:filter_config_interface",
    ],
)

envoy_cc_library(
    name = "factory_base_lib",
    hdrs = ["factory_base.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/server:filter_config_interface",
    ],
)

envoy_cc_library(
    name = "jwks_fetcher_lib",
    srcs = ["jwks_fetcher.cc"],
    hdrs = ["jwks_fetcher.h"],
    deps = [
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/http:utility_lib",
        "@com_github_google_jwt_verify//:jwt_verify_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/jwt_authn/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "stream_rate_limiter_lib",
    srcs = ["stream_rate_limiter.cc"],
    hdrs = ["stream_rate_limiter.h"],
    deps = [
        "//envoy/common:token_bucket_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:token_bucket_impl_lib",
    ],
)

envoy_cc_library(
    name = "ratelimit_headers_lib",
    hdrs = ["ratelimit_headers.h"],
    deps = [
        "//source/common/http:header_map_lib",
    ],
)
