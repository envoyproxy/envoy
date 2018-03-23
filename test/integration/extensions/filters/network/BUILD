licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "echo_integration_test",
    srcs = [
        "echo_integration_test.cc",
    ],
    deps = [
        "//source/extensions/filters/network:echo_lib",
        "//test/integration:integration_lib",
        "//test/server:utility_lib",
    ],
)
