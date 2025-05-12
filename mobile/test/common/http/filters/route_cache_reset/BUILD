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
)

envoy_cc_library(
    name = "route_cache_reset_filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    repository = "@envoy",
    deps = [
        ":filter_cc_proto",
        "@envoy//envoy/http:filter_interface",
        "@envoy//source/common/http:header_map_lib",
        "@envoy//source/common/http:headers_lib",
        "@envoy//source/common/http:utility_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":route_cache_reset_filter_lib",
        "@envoy//source/extensions/filters/http/common:factory_base_lib",
    ],
)
