licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "buffer_helper_lib",
    srcs = ["buffer_helper.cc"],
    hdrs = ["buffer_helper.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:byte_order_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":filter_lib",
        "//include/envoy/registry",
        "//source/common/config:filter_json_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/v2alpha1:thrift_proxy_cc",
    ],
)

envoy_cc_library(
    name = "decoder_lib",
    srcs = ["decoder.cc"],
    hdrs = ["decoder.h"],
    deps = [
        ":protocol_lib",
        ":transport_lib",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        ":decoder_lib",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/stats:timespan",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/network:filter_lib",
    ],
)

envoy_cc_library(
    name = "protocol_lib",
    srcs = [
        "binary_protocol.cc",
        "compact_protocol.cc",
        "protocol.cc",
    ],
    hdrs = [
        "binary_protocol.h",
        "compact_protocol.h",
        "protocol.h",
    ],
    external_deps = ["abseil_optional"],
    deps = [
        ":buffer_helper_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "transport_lib",
    srcs = ["transport.cc"],
    hdrs = ["transport.h"],
    deps = [
        ":buffer_helper_lib",
        ":protocol_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/singleton:const_singleton",
    ],
)
