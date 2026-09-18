load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/common:exception_lib",
        "//envoy/network:filter_interface",
        "//envoy/network:listener_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/stats:stats_macros",
        "//envoy/stream_info:filter_state_interface",
        "//envoy/stream_info:uint32_accessor_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/header_routing:header_parser_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:uint32_accessor_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/session/header_routing/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":filter_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/udp/udp_proxy/session_filters:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/session/header_routing/v3:pkg_cc_proto",
    ],
)
