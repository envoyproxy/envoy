load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "stream_interface",
    hdrs = [
        "stream.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/tracing:trace_context_interface",
    ],
)

envoy_cc_library(
    name = "codec_interface",
    hdrs = [
        "codec.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":stream_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/network:filter_interface",
        "//envoy/server:factory_context_interface",
        "//source/extensions/filters/network/generic_proxy:codec_callbacks_interface",
    ],
)

envoy_cc_library(
    name = "filter_interface",
    hdrs = [
        "filter.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":codec_interface",
        ":stream_interface",
        "//source/extensions/filters/network/generic_proxy:filter_callbacks_interface",
        "//source/extensions/filters/network/generic_proxy:route_interface",
    ],
)
