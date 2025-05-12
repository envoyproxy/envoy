load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "network_filter_fuzzer_fakes_lib",
    hdrs = ["fakes.h"],
    deps = [
        "//source/common/config:metadata_lib",
        "//test/mocks/server:factory_context_mocks",
    ],
)
