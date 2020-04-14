licenses(["notice"])  # Apache 2

# HTTP L7 filter that performs compression with configurable compression libraries
# Public docs: docs/root/configuration/http_filters/compressor_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "compressor_factory_interface",
    hdrs = ["compressor_factory.h"],
    deps = [
        "//include/envoy/compressor:compressor_interface",
    ],
)

envoy_cc_library(
    name = "compressor_filter_lib",
    srcs = ["compressor_filter.cc"],
    hdrs = ["compressor_filter.h"],
    deps = [
        ":compressor_factory_interface",
        "//source/extensions/filters/http/common/compressor:compressor_lib",
        "@envoy_api//envoy/extensions/filters/http/compressor/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "compressor_library_config_interface",
    hdrs = ["compressor_library_config.h"],
    deps = [
        ":compressor_factory_interface",
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/server:filter_config_interface",
    ],
)

envoy_cc_library(
    name = "factory_base_lib",
    hdrs = ["factory_base.h"],
    deps = [
        ":compressor_factory_interface",
        ":compressor_library_config_interface",
        "//include/envoy/server:filter_config_interface",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":compressor_filter_lib",
        ":compressor_library_config_interface",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/compressor/v3:pkg_cc_proto",
    ],
)
