load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_mobile_package",
)
load("@rules_cc//cc:defs.bzl", "cc_binary")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

cc_binary(
    name = "fetch_client",
    srcs = ["fetch_client_main.cc"],
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/common/http:client_lib",
        "//library/common/http:header_utility_lib",
        "//library/common/types:c_types_lib",
        "@com_google_absl//absl/synchronization",
        "@envoy//envoy/http:header_map_interface",
        "@envoy//source/common/http:header_map_lib",
    ],
)
