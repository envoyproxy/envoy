load(
    "@envoy//bazel:envoy_build_system.bzl",
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
    name = "test_accessor_filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    repository = "@envoy",
    deps = [
        "filter_cc_proto",
        "//library/common/api:c_types",
        "//library/common/api:external_api_lib",
        "//library/common/bridge:utility_lib",
        "@envoy//source/common/common:assert_lib",
        "@envoy//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":test_accessor_filter_lib",
        "@envoy//source/extensions/filters/http/common:factory_base_lib",
    ],
)
