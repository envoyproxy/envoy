load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_cc_test",
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
        "@envoy//envoy/http:codes_interface",
        "@envoy//envoy/http:filter_interface",
        "@envoy//source/common/http:header_map_lib",
        "@envoy//source/extensions/common/matcher:matcher_lib",
        "@envoy//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":assertion_filter_lib",
        "@envoy//source/extensions/filters/http/common:factory_base_lib",
    ],
)

envoy_cc_test(
    name = "assertion_filter_test",
    srcs = ["assertion_filter_test.cc"],
    repository = "@envoy",
    deps = [
        ":config",
        ":filter_cc_proto",
        "@envoy//test/mocks/http:http_mocks",
        "@envoy//test/mocks/server:factory_context_mocks",
        "@envoy//test/test_common:utility_lib",
    ],
)
