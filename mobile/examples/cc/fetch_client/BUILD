load("@rules_cc//cc:defs.bzl", "cc_binary")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_mobile_package",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_mobile_package()

cc_binary(
    name = "fetch_client",
    srcs = ["fetch_client_main.cc"],
    deps = [
        "//envoy/http:header_map_interface",
        "//library/cc:engine_builder_lib",
        "//library/common/http:client_lib",
        "//library/common/http:header_utility_lib",
        "//library/common/types:c_types_lib",
        "//source/common/http:header_map_lib",
        "@com_google_absl//absl/synchronization",
    ],
)
