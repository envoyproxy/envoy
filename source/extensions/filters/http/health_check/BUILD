load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# L7 HTTP filter that implements health check responses
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/health_check_filter

envoy_extension_package()

envoy_cc_library(
    name = "health_check_lib",
    srcs = ["health_check.cc"],
    hdrs = ["health_check.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "//envoy/server:filter_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:enum_to_int",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//source/common/http:header_utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/health_check:health_check_lib",
        "@envoy_api//envoy/extensions/filters/http/health_check/v3:pkg_cc_proto",
    ],
)
