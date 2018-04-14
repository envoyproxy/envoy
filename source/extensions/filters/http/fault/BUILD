licenses(["notice"])  # Apache 2
# HTTP L7 filter that injects faults into the request flow
# Public docs: https://github.com/envoyproxy/data-plane-api/blob/master/docs/root/configuration/http_filters/fault_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "fault_filter_lib",
    srcs = ["fault_filter.cc"],
    hdrs = ["fault_filter.h"],
    deps = [
        "//include/envoy/event:timer_interface",
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:config_lib",
        "@envoy_api//envoy/config/filter/http/fault/v2:fault_cc",
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
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/fault:fault_filter_lib",
    ],
)
