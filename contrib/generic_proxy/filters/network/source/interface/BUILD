load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "stream_interface",
    hdrs = [
        "stream.h",
    ],
    deps = [
        "//envoy/tracing:trace_context_interface",
    ],
)

envoy_cc_library(
    name = "codec_callbacks_interface",
    hdrs = [
        "codec_callbacks.h",
    ],
    deps = [
        ":stream_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/network:drain_decision_interface",
    ],
)

envoy_cc_library(
    name = "codec_interface",
    hdrs = [
        "codec.h",
    ],
    deps = [
        ":codec_callbacks_interface",
        ":stream_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/network:filter_interface",
        "//envoy/server:factory_context_interface",
    ],
)

envoy_cc_library(
    name = "filter_interface",
    hdrs = [
        "filter.h",
    ],
    deps = [
        ":codec_interface",
        ":route_interface",
        ":stream_interface",
    ],
)

envoy_cc_library(
    name = "route_interface",
    hdrs = [
        "route.h",
    ],
    deps = [
        ":stream_interface",
        "//envoy/config:typed_metadata_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:connection_interface",
        "//envoy/rds:rds_config_interface",
        "//envoy/router:router_interface",
        "//envoy/stream_info:stream_info_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "config_interface",
    hdrs = [
        "config.h",
    ],
    deps = [
        ":filter_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/server:factory_context_interface",
    ],
)

envoy_cc_library(
    name = "proxy_config_interface",
    hdrs = [
        "proxy_config.h",
    ],
    deps = [
        ":codec_interface",
        ":filter_interface",
        ":route_interface",
        "//contrib/generic_proxy/filters/network/source:access_log_lib",
        "//envoy/tracing:trace_config_interface",
        "//envoy/tracing:tracer_interface",
    ],
)
