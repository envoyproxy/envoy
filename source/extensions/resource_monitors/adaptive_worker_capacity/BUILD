load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "dynamic_token_bucket_lib",
    srcs = ["dynamic_token_bucket.cc"],
    hdrs = ["dynamic_token_bucket.h"],
    deps = [
        "//envoy/common:time_interface",
        "//envoy/server:resource_monitor_interface",
        "//source/common/common:assert_lib",
        "@abseil-cpp//absl/time",
    ],
)
