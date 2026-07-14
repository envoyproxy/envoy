load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "transform_lib",
    srcs = ["transform.cc"],
    hdrs = ["transform.h"],
    deps = [
        "//envoy/server:filter_config_interface",
        "//source/common/config:utility_lib",
        "//source/common/formatter:substitution_format_string_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_mutation_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/transform/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":transform_lib",
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/transform/v3:pkg_cc_proto",
    ],
)
