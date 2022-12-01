load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_proto_library(
    name = "filter",
    srcs = ["filter.proto"],
    deps = [
        "@envoy_api//envoy/config/common/matcher/v3:pkg",
    ],
)

envoy_cc_extension(
    name = "test_event_tracker_filter_lib",
    hdrs = ["filter.h"],
    repository = "@envoy",
    deps = [
        "filter_cc_proto",
        "@envoy//source/common/common:assert_lib",
        "@envoy//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":test_event_tracker_filter_lib",
        "@envoy//source/extensions/filters/http/common:factory_base_lib",
    ],
)
