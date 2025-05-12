load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "app_exception_lib",
    srcs = ["app_exception_impl.cc"],
    hdrs = ["app_exception_impl.h"],
    deps = [
        ":sip_lib",
        ":utility_interface",
        "//envoy/buffer:buffer_interface",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":app_exception_lib",
        ":conn_manager_lib",
        ":decoder_lib",
        ":utility_interface",
        "//contrib/sip_proxy/filters/network/source/filters:filter_config_interface",
        "//contrib/sip_proxy/filters/network/source/filters:well_known_names",
        "//contrib/sip_proxy/filters/network/source/router:router_lib",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/sip_proxy/router/v3alpha:pkg_cc_proto",
        "@envoy_api//contrib/envoy/extensions/filters/network/sip_proxy/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "conn_manager_lib",
    srcs = ["conn_manager.cc"],
    hdrs = ["conn_manager.h"],
    deps = [
        ":app_exception_lib",
        ":decoder_lib",
        ":encoder_lib",
        ":stats_lib",
        ":utility_lib",
        "//contrib/sip_proxy/filters/network/source/router:router_interface",
        "//contrib/sip_proxy/filters/network/source/tra:tra_lib",
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:timespan_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
        "//source/common/common:logger_lib",
        "//source/common/network:filter_lib",
        "//source/common/stats:timespan_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/tracing:http_tracer_lib",
        "@com_google_absl//absl/types:any",
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
        ":stats_lib",
        ":utility_interface",
        "//contrib/sip_proxy/filters/network/source/filters:filter_interface",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_library(
    name = "encoder_lib",
    srcs = ["encoder.cc"],
    hdrs = ["encoder.h"],
    deps = [
        ":app_exception_lib",
        ":stats_lib",
        ":utility_interface",
        "//contrib/sip_proxy/filters/network/source/filters:filter_interface",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_library(
    name = "metadata_lib",
    srcs = ["metadata.cc"],
    hdrs = [
        "metadata.h",
        "operation.h",
    ],
    deps = [
        ":sip_lib",
        "//envoy/buffer:buffer_interface",
        "//source/common/common:macros",
        "//source/common/http:header_map_lib",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//contrib/envoy/extensions/filters/network/sip_proxy/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    deps = [
        ":utility_interface",
        "//contrib/sip_proxy/filters/network/source/filters:filter_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/server:transport_socket_config_interface",
    ],
)

envoy_cc_library(
    name = "utility_interface",
    hdrs = [
        "utility.h",
    ],
    deps = [
        ":decoder_events_lib",
        ":metadata_lib",
        ":sip_lib",
        "//envoy/buffer:buffer_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/singleton:const_singleton",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//contrib/envoy/extensions/filters/network/sip_proxy/tra/v3alpha:pkg_cc_proto",
        "@envoy_api//contrib/envoy/extensions/filters/network/sip_proxy/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "stats_lib",
    hdrs = ["stats.h"],
    deps = [
        ":sip_lib",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
    ],
)

envoy_cc_library(
    name = "conn_state_lib",
    hdrs = ["conn_state.h"],
    deps = [
        "//envoy/tcp:conn_pool_interface",
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
