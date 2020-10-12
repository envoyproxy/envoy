load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Local Ratelimit L7 HTTP filter
# Public docs: docs/root/configuration/http_filters/local_rate_limit_filter.rst

envoy_extension_package()

envoy_cc_library(
    name = "local_ratelimit_lib",
    srcs = ["local_ratelimit.cc"],
    hdrs = ["local_ratelimit.h"],
    deps = [
        "//include/envoy/http:codes_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/common:utility_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/router:header_parser_lib",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/filters/common/local_ratelimit:local_ratelimit_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/local_ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "unknown",
    deps = [
        ":local_ratelimit_lib",
        "//include/envoy/http:filter_interface",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/local_ratelimit/v3:pkg_cc_proto",
    ],
)
