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
        ":thrift_lib",
        "//include/envoy/buffer:buffer_interface",
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
        ":app_exception_lib",
        ":conn_manager_lib",
        ":decoder_lib",
        ":protocol_interface",
        "//include/envoy/registry",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_config_interface",
        "//source/extensions/filters/network/thrift_proxy/filters:well_known_names",
        "//source/extensions/filters/network/thrift_proxy/router:router_lib",
        "@envoy_api//envoy/config/filter/network/thrift_proxy/v2alpha1:thrift_proxy_cc",
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
    name = "decoder_events_lib",
    hdrs = ["decoder_events.h"],
    deps = [
        ":metadata_lib",
        ":thrift_lib",
    ],
)

envoy_cc_library(
    name = "decoder_lib",
    srcs = ["decoder.cc"],
    hdrs = ["decoder.h"],
    deps = [
        ":app_exception_lib",
        ":protocol_interface",
        ":stats_lib",
        ":transport_interface",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_interface",
    ],
)

envoy_cc_library(
    name = "metadata_lib",
    hdrs = ["metadata.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":thrift_lib",
        ":tracing_interface",
        "//include/envoy/buffer:buffer_interface",
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "tracing_interface",
    hdrs = ["tracing.h"],
    external_deps = ["abseil_optional"],
    deps = [],
)

envoy_cc_library(
    name = "protocol_converter_lib",
    hdrs = [
        "protocol_converter.h",
    ],
    deps = [
        ":decoder_events_lib",
        ":protocol_interface",
        "//include/envoy/buffer:buffer_interface",
    ],
)

envoy_cc_library(
    name = "protocol_interface",
    hdrs = [
        "protocol.h",
    ],
    external_deps = ["abseil_optional"],
    deps = [
        ":conn_state_lib",
        ":decoder_events_lib",
        ":metadata_lib",
        ":thrift_lib",
        ":thrift_object_interface",
        ":transport_interface",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/singleton:const_singleton",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_interface",
    ],
)

envoy_cc_library(
    name = "auto_protocol_lib",
    srcs = [
        "auto_protocol_impl.cc",
    ],
    hdrs = [
        "auto_protocol_impl.h",
    ],
    deps = [
        ":binary_protocol_lib",
        ":buffer_helper_lib",
        ":compact_protocol_lib",
        ":protocol_interface",
        ":twitter_protocol_lib",
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "binary_protocol_lib",
    srcs = [
        "binary_protocol_impl.cc",
    ],
    hdrs = [
        "binary_protocol_impl.h",
    ],
    deps = [
        ":buffer_helper_lib",
        ":protocol_interface",
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "compact_protocol_lib",
    srcs = [
        "compact_protocol_impl.cc",
    ],
    hdrs = [
        "compact_protocol_impl.h",
    ],
    external_deps = ["abseil_optional"],
    deps = [
        ":buffer_helper_lib",
        ":protocol_interface",
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "twitter_protocol_lib",
    srcs = [
        "twitter_protocol_impl.cc",
    ],
    hdrs = [
        "twitter_protocol_impl.h",
    ],
    deps = [
        ":binary_protocol_lib",
        ":buffer_helper_lib",
        ":protocol_interface",
        ":thrift_object_lib",
        "//source/common/common:macros",
    ],
)

envoy_cc_library(
    name = "protocol_lib",
    deps = [
        ":auto_protocol_lib",
        ":binary_protocol_lib",
        ":compact_protocol_lib",
        ":twitter_protocol_lib",
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
        ":buffer_helper_lib",
        ":metadata_lib",
        ":thrift_lib",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "conn_state_lib",
    hdrs = ["conn_state.h"],
    deps = [
        "//include/envoy/tcp:conn_pool_interface",
    ],
)

envoy_cc_library(
    name = "thrift_lib",
    hdrs = ["thrift.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "thrift_object_interface",
    hdrs = ["thrift_object.h"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
    ],
)

envoy_cc_library(
    name = "thrift_object_lib",
    srcs = ["thrift_object_impl.cc"],
    hdrs = ["thrift_object_impl.h"],
    deps = [
        ":decoder_lib",
        ":thrift_lib",
        ":thrift_object_interface",
        ":unframed_transport_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:filter_interface",
    ],
)

envoy_cc_library(
    name = "auto_transport_lib",
    srcs = [
        "auto_transport_impl.cc",
    ],
    hdrs = [
        "auto_transport_impl.h",
    ],
    deps = [
        ":buffer_helper_lib",
        ":framed_transport_lib",
        ":header_transport_lib",
        ":protocol_lib",
        ":transport_interface",
        ":unframed_transport_lib",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "framed_transport_lib",
    srcs = [
        "framed_transport_impl.cc",
    ],
    hdrs = [
        "framed_transport_impl.h",
    ],
    deps = [
        ":buffer_helper_lib",
        ":transport_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "header_transport_lib",
    srcs = [
        "header_transport_impl.cc",
    ],
    hdrs = [
        "header_transport_impl.h",
    ],
    deps = [
        ":app_exception_lib",
        ":buffer_helper_lib",
        ":transport_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "unframed_transport_lib",
    srcs = [
        "unframed_transport_impl.cc",
    ],
    hdrs = [
        "unframed_transport_impl.h",
    ],
    deps = [
        ":buffer_helper_lib",
        ":transport_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "transport_lib",
    deps = [
        ":auto_transport_lib",
        ":framed_transport_lib",
        ":header_transport_lib",
        ":unframed_transport_lib",
    ],
)
