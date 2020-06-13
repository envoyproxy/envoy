licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

envoy_package()

envoy_cc_mock(
    name = "runtime_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:empty_string",
        "//test/mocks:common_lib",
    ],
)
