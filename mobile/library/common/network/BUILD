licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

envoy_package()

envoy_cc_library(
    name = "synthetic_address_lib",
    hdrs = ["synthetic_address_impl.h"],
    repository = "@envoy",
    deps = ["@envoy//include/envoy/network:address_interface"],
)
