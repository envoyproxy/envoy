load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "basic_auth_lib",
    srcs = ["basic_auth_filter.cc"],
    hdrs = ["basic_auth_filter.h"],
    external_deps = ["ssl"],
    deps = [
        "//envoy/server:filter_config_interface",
        "//source/common/common:base64_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/basic_auth/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":basic_auth_lib",
        "//envoy/registry",
        "//source/common/config:datasource_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/basic_auth/v3:pkg_cc_proto",
    ],
)
