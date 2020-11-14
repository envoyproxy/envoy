load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Redis custom health checker.

envoy_extension_package()

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//source/common/matcher:matcher_lib",
    ],
    #security_posture = "unknown",
)
