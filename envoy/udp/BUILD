load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "hash_policy_interface",
    hdrs = ["hash_policy.h"],
    deps = [
        "//envoy/network:address_interface",
        "@com_google_absl//absl/types:optional",
    ],
)
