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
    name = "platform_bridge_filter_lib",
    srcs = [
        "c_type_definitions.h",
        "filter.cc",
    ],
    hdrs = [
        "c_types.h",
        "filter.h",
    ],
    category = "envoy.filters.http",
    repository = "@envoy",
    security_posture = "requires_trusted_downstream_and_upstream",
    deps = [
        ":filter_cc_proto",
        "//library/common/api:external_api_lib",
        "//library/common/buffer:utility_lib",
        "//library/common/http:header_utility_lib",
        "//library/common/http:internal_headers_lib",
        "//library/common/types:c_types_lib",
        "@envoy//include/envoy/http:filter_interface",
        "@envoy//source/common/common:minimal_logger_lib",
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
        ":platform_bridge_filter_lib",
        "@envoy//source/extensions/filters/http/common:factory_base_lib",
    ],
)
