load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# HTTP L7 filter that performs decompression with configurable decompression libraries
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/decompressor_filter

envoy_extension_package()

envoy_cc_library(
    name = "decompressor_filter_lib",
    srcs = ["decompressor_filter.cc"],
    hdrs = ["decompressor_filter.h"],
    deps = [
        "//envoy/compression/decompressor:decompressor_config_interface",
        "//envoy/compression/decompressor:decompressor_interface",
        "//envoy/http:filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:macros",
        "//source/common/http:headers_lib",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/decompressor/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":decompressor_filter_lib",
        "//envoy/compression/decompressor:decompressor_config_interface",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/decompressor/v3:pkg_cc_proto",
    ],
)
