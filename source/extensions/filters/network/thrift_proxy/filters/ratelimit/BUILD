licenses(["notice"])  # Apache 2

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
        "//include/envoy/ratelimit:ratelimit_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/protobuf:utility_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/extensions/filters/network/thrift_proxy:app_exception_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_interface",
        "//source/extensions/filters/network/thrift_proxy/router:router_ratelimit_interface",
        "@envoy_api//envoy/config/filter/thrift/rate_limit/v2alpha1:rate_limit_cc",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":ratelimit_lib",
        "//include/envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:factory_base_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:well_known_names",
    ],
)
