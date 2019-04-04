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
    name = "hessian_utils_lib",
    srcs = ["hessian_utils.cc"],
    hdrs = ["hessian_utils.h"],
    deps = [
        ":buffer_helper_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "protocol_interface",
    hdrs = ["protocol.h"],
    deps = [
        ":buffer_helper_lib",
        ":message_lib",
        ":metadata_lib",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "dubbo_protocol_impl_lib",
    srcs = ["dubbo_protocol_impl.cc"],
    hdrs = ["dubbo_protocol_impl.h"],
    deps = [
        ":protocol_interface",
        "//include/envoy/buffer:buffer_interface",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "deserializer_interface",
    srcs = ["deserializer_impl.cc"],
    hdrs = [
        "deserializer.h",
        "deserializer_impl.h",
    ],
    deps = [
        ":message_lib",
        "//include/envoy/buffer:buffer_interface",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "hessian_deserializer_impl_lib",
    srcs = ["hessian_deserializer_impl.cc"],
    hdrs = [
        "hessian_deserializer_impl.h",
    ],
    deps = [
        ":buffer_helper_lib",
        ":deserializer_interface",
        ":hessian_utils_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "decoder_lib",
    srcs = ["decoder.cc"],
    hdrs = ["decoder.h"],
    deps = [
        ":dubbo_protocol_impl_lib",
        ":hessian_deserializer_impl_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        ":decoder_lib",
        ":stats_lib",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/stats:timespan",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/network:filter_lib",
        "@envoy_api//envoy/config/filter/network/dubbo_proxy/v2alpha1:dubbo_proxy_cc",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":filter_lib",
        "//include/envoy/registry",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/config/filter/network/dubbo_proxy/v2alpha1:dubbo_proxy_cc",
    ],
)

envoy_cc_library(
    name = "metadata_lib",
    hdrs = ["metadata.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":message_lib",
        "//source/common/http:header_map_lib",
    ],
)

envoy_cc_library(
    name = "message_lib",
    hdrs = ["message.h"],
)

envoy_cc_library(
    name = "decoder_events_lib",
    hdrs = ["decoder_event_handler.h"],
    deps = [
        ":metadata_lib",
        "//include/envoy/network:filter_interface",
        "//source/common/buffer:buffer_lib",
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
    name = "app_exception_lib",
    srcs = ["app_exception.cc"],
    hdrs = ["app_exception.h"],
    deps = [
        ":deserializer_interface",
        ":message_lib",
        ":metadata_lib",
        ":protocol_interface",
        "//include/envoy/buffer:buffer_interface",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/dubbo_proxy/filters:filter_interface",
    ],
)
