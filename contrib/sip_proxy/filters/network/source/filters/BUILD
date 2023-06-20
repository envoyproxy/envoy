load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "filter_config_interface",
    hdrs = ["filter_config.h"],
    deps = [
        ":filter_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/server:filter_config_interface",
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
        "//contrib/sip_proxy/filters/network/source:decoder_events_lib",
        "//contrib/sip_proxy/filters/network/source:sip_lib",
        "//contrib/sip_proxy/filters/network/source:stats_lib",
        "//contrib/sip_proxy/filters/network/source:utility_interface",
        "//contrib/sip_proxy/filters/network/source/router:router_interface",
        "//contrib/sip_proxy/filters/network/source/tra:tra_lib",
        "//envoy/buffer:buffer_interface",
        "//envoy/network:connection_interface",
        "//envoy/stream_info:stream_info_interface",
    ],
)

envoy_cc_library(
    name = "well_known_names",
    hdrs = ["well_known_names.h"],
    deps = [
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "pass_through_filter_lib",
    hdrs = ["pass_through_filter.h"],
    deps = [
        ":filter_interface",
    ],
)
