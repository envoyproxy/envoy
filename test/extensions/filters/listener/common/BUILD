load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "listener_filter_fuzz_test_proto",
    srcs = ["listener_filter_fuzz_test.proto"],
)

envoy_cc_test_library(
    name = "uber_filter_lib",
    srcs = [
        "uber_filter.cc",
    ],
    hdrs = ["uber_filter.h"],
    deps = [
        ":listener_filter_fuzz_test_proto_cc_proto",
        "//test/mocks/network:network_fakes",
        "//test/mocks/network:network_mocks",
    ],
)
