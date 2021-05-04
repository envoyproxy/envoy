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
)

envoy_cc_extension(
    name = "route_cache_reset_filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    category = "envoy.filters.http",
    repository = "@envoy",
    security_posture = "requires_trusted_downstream_and_upstream",
    deps = [
        ":filter_cc_proto",
        "@envoy//include/envoy/http:filter_interface",
        "@envoy//source/common/http:header_map_lib",
        "@envoy//source/common/http:headers_lib",
        "@envoy//source/common/http:utility_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    category = "envoy.filters.http",
    repository = "@envoy",
    security_posture = "requires_trusted_downstream_and_upstream",
    deps = [
        ":route_cache_reset_filter_lib",
        "@envoy//source/extensions/filters/http/common:factory_base_lib",
    ],
)
