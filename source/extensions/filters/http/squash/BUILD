load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# L7 HTTP filter that implements the Squash microservice debugger
# Public docs: docs/root/configuration/http_filters/squash_filter.rst

envoy_extension_package()

envoy_cc_library(
    name = "squash_filter_lib",
    srcs = ["squash_filter.cc"],
    hdrs = ["squash_filter.h"],
    deps = [
        "//include/envoy/event:timer_interface",
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/squash/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "requires_trusted_downstream_and_upstream",
    deps = [
        "//include/envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/squash:squash_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/squash/v3:pkg_cc_proto",
    ],
)
