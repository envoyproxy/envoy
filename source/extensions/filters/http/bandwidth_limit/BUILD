load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Local Bandwidthlimit HTTP L7 filter
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/bandwidth_limit_filter

envoy_extension_package()

envoy_cc_library(
    name = "bandwidth_limit_lib",
    srcs = ["bandwidth_limit.cc"],
    hdrs = ["bandwidth_limit.h"],
    deps = [
        "//envoy/http:codes_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/stats:stats_macros",
        "//source/common/common:shared_token_bucket_impl_lib",
        "//source/common/common:utility_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/router:header_parser_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/stats:timespan_lib",
        "//source/extensions/filters/http/common:stream_rate_limiter_lib",
        "@envoy_api//envoy/extensions/filters/http/bandwidth_limit/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":bandwidth_limit_lib",
        "//envoy/http:filter_interface",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/bandwidth_limit/v3:pkg_cc_proto",
    ],
)
