load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":header_to_metadata_filter_lib",
        "//envoy/registry",
        "//source/extensions/filters/network/thrift_proxy/filters:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/filters/header_to_metadata/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "header_to_metadata_filter_lib",
    srcs = ["header_to_metadata_filter.cc"],
    hdrs = ["header_to_metadata_filter.h"],
    deps = [
        "//envoy/server:filter_config_interface",
        "//source/common/common:base64_lib",
        "//source/common/common:matchers_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/filters/network/thrift_proxy/filters:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/filters/header_to_metadata/v3:pkg_cc_proto",
    ],
)
