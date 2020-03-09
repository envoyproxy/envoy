licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "utility_test",
    srcs = [
        "utility_test.cc",
    ],
    deps = [
        "//source/extensions/common:utility_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)
