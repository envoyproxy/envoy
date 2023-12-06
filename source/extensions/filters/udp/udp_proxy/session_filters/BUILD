load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

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
    visibility = ["//visibility:public"],
    deps = [
        ":filter_config_interface",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "filter_interface",
    hdrs = ["filter.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/network:listener_interface",
        "//envoy/stream_info:stream_info_interface",
    ],
)

envoy_cc_library(
    name = "pass_through_filter_lib",
    hdrs = ["pass_through_filter.h"],
    deps = [
        ":filter_interface",
    ],
)
