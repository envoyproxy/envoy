licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "registry",
    hdrs = ["registry.h"],
    deps = [
        "//source/common/common:assert_lib",
    ],
)
