load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "generic_stream_interface",
    hdrs = [
        "generic_stream.h",
    ],
)

envoy_cc_library(
    name = "generic_codec_interface",
    hdrs = [
        "generic_codec.h",
    ],
    deps = [
        ":generic_stream_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/server:factory_context_interface",
    ],
)

envoy_cc_library(
    name = "generic_filter_interface",
    hdrs = [
        "generic_filter.h",
    ],
    deps = [
        ":generic_codec_interface",
        ":generic_route_interface",
        ":generic_stream_interface",
    ],
)

envoy_cc_library(
    name = "generic_route_interface",
    hdrs = [
        "generic_route.h",
    ],
    deps = [
        ":generic_stream_interface",
        "//envoy/config:typed_metadata_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:connection_interface",
        "//envoy/stream_info:stream_info_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "generic_config_interface",
    hdrs = [
        "generic_config.h",
    ],
    deps = [
        ":generic_filter_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/server:factory_context_interface",
    ],
)
