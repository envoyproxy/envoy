load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_mobile_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_proto_library(
    name = "filter",
    srcs = ["filter.proto"],
    deps = [
        "@envoy_api//envoy/config/common/matcher/v3:pkg",
    ],
)

envoy_cc_library(
    name = "assertion_filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    repository = "@envoy",
    deps = [
        "filter_cc_proto",
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "//source/common/http:header_map_lib",
        "//source/extensions/common/matcher:matcher_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":assertion_filter_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
    ],
)
