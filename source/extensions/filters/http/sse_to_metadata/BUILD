load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "sse_to_metadata_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        "//envoy/content_parser:config_interface",
        "//envoy/content_parser:factory_interface",
        "//envoy/content_parser:parser_interface",
        "//envoy/server:filter_config_interface",
        "//source/common/http:header_utility_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/sse:sse_parser_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/sse_to_metadata/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":sse_to_metadata_lib",
        "//envoy/content_parser:config_interface",
        "//envoy/registry",
        "//source/extensions/content_parsers/json:config",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/sse_to_metadata/v3:pkg_cc_proto",
    ],
)
