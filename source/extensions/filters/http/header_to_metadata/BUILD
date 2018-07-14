licenses(["notice"])  # Apache 2

# HTTP L7 filter that transforms request data into dynamic metadata
# Public docs: docs/root/configuration/http_filters/header_to_metadata_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "header_to_metadata_filter_lib",
    srcs = ["header_to_metadata_filter.cc"],
    hdrs = ["header_to_metadata_filter.h"],
    deps = [
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http:well_known_names",
        "@envoy_api//envoy/config/filter/http/header_to_metadata/v2:header_to_metadata_cc",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/header_to_metadata:header_to_metadata_filter_lib",
    ],
)
