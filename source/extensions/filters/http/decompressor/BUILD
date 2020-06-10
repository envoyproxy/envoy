load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# HTTP L7 filter that performs decompression with configurable decompression libraries
# Public docs: docs/root/configuration/http_filters/decompressor_filter.rst

envoy_package()

envoy_cc_library(
    name = "decompressor_filter_lib",
    srcs = ["decompressor_filter.cc"],
    hdrs = ["decompressor_filter.h"],
    deps = [
        "//include/envoy/compression/decompressor:decompressor_config_interface",
        "//include/envoy/compression/decompressor:decompressor_interface",
        "//include/envoy/http:filter_interface",
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
    security_posture = "robust_to_untrusted_downstream_and_upstream",
    deps = [
        ":decompressor_filter_lib",
        "//include/envoy/compression/decompressor:decompressor_config_interface",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/decompressor/v3:pkg_cc_proto",
    ],
)
