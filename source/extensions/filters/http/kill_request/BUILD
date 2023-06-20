load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package(enabled_default = False)

envoy_cc_library(
    name = "kill_request_filter_lib",
    srcs = ["kill_request_filter.cc"],
    hdrs = ["kill_request_filter.h"],
    deps = [
        "//envoy/common:random_generator_interface",
        "//envoy/http:filter_interface",
        "//envoy/http:header_map_interface",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/kill_request/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "kill_request_config",
    srcs = ["kill_request_config.cc"],
    hdrs = ["kill_request_config.h"],
    deps = [
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/kill_request:kill_request_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/kill_request/v3:pkg_cc_proto",
    ],
)
