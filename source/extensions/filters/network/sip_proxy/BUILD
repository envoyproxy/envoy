load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "app_exception_lib",
    srcs = ["app_exception_impl.cc"],
    hdrs = ["app_exception_impl.h"],
    deps = [
        ":protocol_interface",
        ":sip_lib",
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

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    category = "envoy.filters.network",
    security_posture = "requires_trusted_downstream_and_upstream",
    deps = [
        ":app_exception_lib",
        ":conn_manager_lib",
        ":decoder_lib",
        ":protocol_interface",
        "//include/envoy/registry",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/sip_proxy/filters:filter_config_interface",
        "//source/extensions/filters/network/sip_proxy/filters:well_known_names",
        "//source/extensions/filters/network/sip_proxy/router:router_lib",
        "@envoy_api//envoy/extensions/filters/network/sip_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "conn_manager_lib",
    srcs = ["conn_manager.cc"],
    hdrs = ["conn_manager.h"],
    external_deps = ["abseil_any"],
    deps = [
        ":app_exception_lib",
        ":decoder_lib",
        ":encoder_lib",
        ":protocol_interface",
        ":stats_lib",
        ":transport_interface",
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:timespan_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
        "//source/common/common:logger_lib",
        "//source/common/network:filter_lib",
        "//source/common/stats:timespan_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/network/sip_proxy/router:router_interface",
    ],
)

envoy_cc_library(
    name = "decoder_events_lib",
    hdrs = ["decoder_events.h"],
    deps = [
        ":metadata_lib",
        ":sip_lib",
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
        "//source/extensions/filters/network/sip_proxy/filters:filter_interface",
#        "//source/extensions/filters/network/sip_proxy:sip_parser_lib",
    ],
)

envoy_cc_library(
    name = "encoder_lib",
    srcs = ["encoder.cc"],
    hdrs = ["encoder.h"],
    deps = [
        ":app_exception_lib",
        ":protocol_interface",
        ":stats_lib",
        ":transport_interface",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/sip_proxy/filters:filter_interface",
#        "//source/extensions/filters/network/sip_proxy:sip_parser_lib",
    ],
)

envoy_cc_library(
    name = "metadata_lib",
    hdrs = ["metadata.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":sip_lib",
        ":tracing_interface",
        "//include/envoy/buffer:buffer_interface",
        "//source/common/common:macros",
        "//source/common/http:header_map_lib",
    ],
)

envoy_cc_library(
    name = "tracing_interface",
    hdrs = ["tracing.h"],
    external_deps = ["abseil_optional"],
    deps = [],
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
        ":sip_lib",
        ":transport_interface",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
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
        ":buffer_helper_lib",
        ":metadata_lib",
        ":sip_lib",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/config:typed_config_interface",
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
    name = "sip_lib",
    hdrs = ["sip.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/singleton:const_singleton",
    ],
)
