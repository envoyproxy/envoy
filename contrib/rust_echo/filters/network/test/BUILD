load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "echo_integration_test",
    srcs = [
        "echo_integration_test.cc",
    ],
    tags = [
        # Uncomment this line to run this test repeatedly in exclusive mode if not using docker-sandbox,
        # or RBE, see comments in AddRemoveListener.
        # "exclusive",
    ],
    deps = [
        "//contrib/rust_echo/filters/network/source:config",
        "//test/integration:integration_lib",
        "//test/server:utility_lib",
        "//test/test_common:utility_lib",
    ],
)
