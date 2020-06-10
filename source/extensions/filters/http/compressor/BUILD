load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# HTTP L7 filter that performs compression with configurable compression libraries
# Public docs: docs/root/configuration/http_filters/compressor_filter.rst

envoy_package()

envoy_cc_library(
    name = "compressor_filter_lib",
    srcs = ["compressor_filter.cc"],
    hdrs = ["compressor_filter.h"],
    deps = [
        "//include/envoy/compression/compressor:compressor_factory_interface",
        "//source/extensions/filters/http/common/compressor:compressor_lib",
        "@envoy_api//envoy/extensions/filters/http/compressor/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":compressor_filter_lib",
        "//include/envoy/compression/compressor:compressor_config_interface",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/compressor/v3:pkg_cc_proto",
    ],
)
