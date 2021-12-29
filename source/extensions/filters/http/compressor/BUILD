load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# HTTP L7 filter that performs compression with configurable compression libraries
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/compressor_filter

envoy_extension_package()

envoy_cc_library(
    name = "compressor_filter_lib",
    srcs = ["compressor_filter.cc"],
    hdrs = ["compressor_filter.h"],
    deps = [
        "//envoy/compression/compressor:compressor_factory_interface",
        "//envoy/stats:stats_macros",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/compressor/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":compressor_filter_lib",
        "//envoy/compression/compressor:compressor_config_interface",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/compressor/v3:pkg_cc_proto",
    ],
)
