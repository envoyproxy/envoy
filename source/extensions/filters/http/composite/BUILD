load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# HTTP filter that allows delegating to another HTTP filter as specified by a match action.

envoy_extension_package()

envoy_cc_library(
    name = "action_lib",
    srcs = ["action.cc"],
    hdrs = ["action.h"],
    deps = [
        "//source/common/http:filter_chain_helper_lib",
        "//source/common/http/matching:data_impl_lib",
        "//source/common/matcher:matcher_lib",
        "@envoy_api//envoy/extensions/filters/http/composite/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    srcs = [
        "factory_wrapper.cc",
        "filter.cc",
    ],
    hdrs = [
        "factory_wrapper.h",
        "filter.h",
    ],
    deps = [
        ":action_lib",
        "//envoy/http:filter_interface",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/matcher:matcher_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/composite:filter_lib",
        "@envoy_api//envoy/extensions/filters/http/composite/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)
