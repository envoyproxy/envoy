load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

# L7 HTTP filter that implements the Squash microservice debugger
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/squash_filter

envoy_contrib_package()

envoy_cc_library(
    name = "squash_filter_lib",
    srcs = ["squash_filter.cc"],
    hdrs = ["squash_filter.h"],
    deps = [
        "//envoy/event:timer_interface",
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "//envoy/http:header_map_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/squash/v3:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":squash_filter_lib",
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/squash/v3:pkg_cc_proto",
    ],
)
