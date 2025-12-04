load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_mobile_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_mobile_package()

# Mock DNS resolver proto and implementation for tests.
envoy_proto_library(
    name = "mock_dns_resolver_proto",
    srcs = ["mock_dns_resolver.proto"],
)

envoy_cc_library(
    name = "mock_dns_resolver_lib",
    srcs = ["mock_dns_resolver.cc"],
    hdrs = ["mock_dns_resolver.h"],
    repository = "@envoy",
    deps = [
        ":mock_dns_resolver_proto_cc_proto",
        "@com_google_absl//absl/strings",
        "@envoy//envoy/api:api_interface",
        "@envoy//envoy/event:dispatcher_interface",
        "@envoy//envoy/network:dns_interface",
        "@envoy//envoy/network:dns_resolver_interface",
        "@envoy//source/common/config:utility_lib",
        "@envoy//source/common/network:address_lib",
        "@envoy//source/common/network:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
    alwayslink = 1,
)
