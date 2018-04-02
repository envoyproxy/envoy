licenses(["notice"])  # Apache 2
# Ratelimit L4 network filter
# Public docs: https://github.com/envoyproxy/data-plane-api/blob/master/docs/root/configuration/network_filters/rate_limit_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "ratelimit_lib",
    srcs = ["ratelimit.cc"],
    hdrs = ["ratelimit.h"],
    deps = [
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/ratelimit:ratelimit_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/tracing:http_tracer_lib",
        "@envoy_api//envoy/config/filter/network/rate_limit/v2:rate_limit_cc",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/network/ratelimit:ratelimit_lib",
    ],
)
