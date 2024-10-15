load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "tcp_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    rbe_pool = "2core",
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/tcp:conn_pool_interface",
        "//test/mocks:common_lib",
        "//test/mocks/network:io_handle_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/upstream:host_mocks",
    ],
)
