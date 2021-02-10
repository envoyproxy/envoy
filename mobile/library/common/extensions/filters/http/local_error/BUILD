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
    name = "local_error_filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    category = "envoy.filters.http",
    repository = "@envoy",
    security_posture = "requires_trusted_downstream_and_upstream",
    deps = [
        ":filter_cc_proto",
        "//library/common/http:internal_headers_lib",
        "//library/common/types:c_types_lib",
        "@envoy//include/envoy/http:codes_interface",
        "@envoy//include/envoy/http:filter_interface",
        "@envoy//source/common/grpc:common_lib",
        "@envoy//source/common/grpc:status_lib",
        "@envoy//source/common/http:codes_lib",
        "@envoy//source/common/http:header_map_lib",
        "@envoy//source/common/http:headers_lib",
        "@envoy//source/common/http:utility_lib",
        "@envoy//source/extensions/filters/http/common:pass_through_filter_lib",
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
        ":local_error_filter_lib",
        "@envoy//source/extensions/filters/http/common:factory_base_lib",
    ],
)
