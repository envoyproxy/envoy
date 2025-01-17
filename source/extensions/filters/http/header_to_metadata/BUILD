load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# HTTP L7 filter that transforms request data into dynamic metadata
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/header_to_metadata_filter

envoy_extension_package()

envoy_cc_library(
    name = "header_to_metadata_filter_lib",
    srcs = ["header_to_metadata_filter.cc"],
    hdrs = ["header_to_metadata_filter.h"],
    deps = [
        "//envoy/server:filter_config_interface",
        "//source/common/common:base64_lib",
        "//source/common/config:well_known_names",
        "//source/common/http:header_utility_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/http:well_known_names",
        "@envoy_api//envoy/extensions/filters/http/header_to_metadata/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/header_to_metadata:header_to_metadata_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/header_to_metadata/v3:pkg_cc_proto",
    ],
)
