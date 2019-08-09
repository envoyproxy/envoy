licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

envoy_package()

envoy_cc_library(
    name = "c_types_interface",
    srcs = ["c_types.cc"],
    hdrs = ["c_types.h"],
    repository = "@envoy",
)
