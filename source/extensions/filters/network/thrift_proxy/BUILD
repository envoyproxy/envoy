licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "app_exception_lib",
    srcs = ["app_exception_impl.cc"],
    hdrs = ["app_exception_impl.h"],
    deps = [
        ":protocol_interface",
        "//include/envoy/buffer:buffer_interface",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_interface",
    ],
)

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
        ":conn_manager_lib",
        ":decoder_lib",
        ":protocol_lib",
        "//include/envoy/registry",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_config_interface",
        "//source/extensions/filters/network/thrift_proxy/filters:well_known_names",
        "//source/extensions/filters/network/thrift_proxy/router:router_lib",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/v2alpha1:thrift_proxy_cc",
    ],
)

envoy_cc_library(
    name = "conn_manager_lib",
    srcs = ["conn_manager.cc"],
    hdrs = ["conn_manager.h"],
    deps = [
        ":app_exception_lib",
        ":decoder_lib",
        ":protocol_converter_lib",
        ":protocol_lib",
        ":stats_lib",
        ":transport_lib",
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:timespan",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
        "//source/common/common:logger_lib",
        "//source/common/network:filter_lib",
        "//source/extensions/filters/network/thrift_proxy/router:router_interface",
    ],
)

envoy_cc_library(
    name = "decoder_lib",
    srcs = ["decoder.cc"],
    hdrs = ["decoder.h"],
    deps = [
        ":protocol_lib",
        ":stats_lib",
        ":transport_lib",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_interface",
    ],
)

envoy_cc_library(
    name = "protocol_converter_lib",
    hdrs = [
        "protocol_converter.h",
    ],
    deps = [
        ":protocol_interface",
        "//include/envoy/buffer:buffer_interface",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_interface",
    ],
)

envoy_cc_library(
    name = "protocol_interface",
    hdrs = [
        "protocol.h",
    ],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "protocol_lib",
    srcs = [
        "binary_protocol_impl.cc",
        "compact_protocol_impl.cc",
        "protocol_impl.cc",
    ],
    hdrs = [
        "binary_protocol_impl.h",
        "compact_protocol_impl.h",
        "protocol_impl.h",
    ],
    external_deps = ["abseil_optional"],
    deps = [
        ":buffer_helper_lib",
        ":protocol_interface",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "stats_lib",
    hdrs = ["stats.h"],
    deps = [
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
    ],
)

envoy_cc_library(
    name = "transport_interface",
    hdrs = ["transport.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "transport_lib",
    srcs = [
        "framed_transport_impl.cc",
        "transport_impl.cc",
        "unframed_transport_impl.cc",
    ],
    hdrs = [
        "framed_transport_impl.h",
        "transport_impl.h",
        "unframed_transport_impl.h",
    ],
    deps = [
        ":buffer_helper_lib",
        ":protocol_lib",
        ":transport_interface",
        "//source/common/common:assert_lib",
        "//source/common/singleton:const_singleton",
    ],
)
