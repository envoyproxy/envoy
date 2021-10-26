load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "generic_stream_interface",
    hdrs = [
        "generic_stream.h",
    ],
    repository = "@envoy",
)

envoy_cc_library(
    name = "generic_codec_interface",
    hdrs = [
        "generic_codec.h",
    ],
    repository = "@envoy",
    deps = [
        ":generic_stream_interface",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/server:factory_context_interface",
    ],
)

envoy_cc_library(
    name = "generic_filter_interface",
    hdrs = [
        "generic_filter.h",
    ],
    repository = "@envoy",
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
    repository = "@envoy",
    deps = [
        ":generic_stream_interface",
        "//include/envoy/config:typed_metadata_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/stream_info:stream_info_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "generic_config_interface",
    hdrs = [
        "generic_config.h",
    ],
    repository = "@envoy",
    deps = [
        ":generic_filter_interface",
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/server:factory_context_interface",
    ],
)
