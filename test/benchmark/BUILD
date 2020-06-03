load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "main",
    srcs = ["main.cc"],
    external_deps = [
        "benchmark",
    ],
    deps = [
        "//test/test_common:environment_lib",
    ],
)
