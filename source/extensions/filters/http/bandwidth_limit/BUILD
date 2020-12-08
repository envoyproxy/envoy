load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

# Local Bandwidthlimit HTTP L7 filter
# Public docs: docs/root/configuration/http_filters/bandwidth_limit_filter.rst

envoy_extension_package()

envoy_cc_library(
    name = "bandwidth_limit_lib",
    srcs = ["bandwidth_limit.cc"],
    hdrs = ["bandwidth_limit.h"],
    deps = [
        "//include/envoy/http:codes_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/common:utility_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/router:header_parser_lib",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/filters/http/common:stream_rate_limiter_lib",
        "@envoy_api//envoy/extensions/filters/http/bandwidth_limit/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "unknown",
    deps = [
        ":bandwidth_limit_lib",
        "//include/envoy/http:filter_interface",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/bandwidth_limit/v3alpha:pkg_cc_proto",
    ],
)
