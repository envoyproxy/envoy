load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "reverse_connection_utility_test",
    size = "medium",
    srcs = ["reverse_connection_utility_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/network:connection_lib",
        "//source/extensions/bootstrap/reverse_tunnel/common:reverse_connection_utility_lib",
        "//test/mocks/network:network_mocks",
        "//test/test_common:test_runtime_lib",
    ],
)
