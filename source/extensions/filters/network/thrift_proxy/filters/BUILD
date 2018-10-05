licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "filter_config_interface",
    hdrs = ["filter_config.h"],
    deps = [
        ":filter_interface",
        "//include/envoy/server:filter_config_interface",
        "//source/common/common:macros",
        "//source/common/protobuf:cc_wkt_protos",
    ],
)

envoy_cc_library(
    name = "factory_base_lib",
    hdrs = ["factory_base.h"],
    deps = [
        ":filter_config_interface",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "filter_interface",
    hdrs = ["filter.h"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/stream_info:stream_info_interface",
        "//source/extensions/filters/network/thrift_proxy:decoder_events_lib",
        "//source/extensions/filters/network/thrift_proxy:protocol_interface",
        "//source/extensions/filters/network/thrift_proxy:thrift_lib",
        "//source/extensions/filters/network/thrift_proxy/router:router_interface",
    ],
)

envoy_cc_library(
    name = "well_known_names",
    hdrs = ["well_known_names.h"],
    deps = [
        "//source/common/singleton:const_singleton",
    ],
)
