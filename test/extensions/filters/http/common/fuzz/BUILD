licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test_library(
    name = "decoder_filter_fuzz_lib",
    hdrs = ["decoder_filter_fuzz.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//test/fuzz:utility_lib",
    ],
)
