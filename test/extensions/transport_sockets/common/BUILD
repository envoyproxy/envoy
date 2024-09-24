load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "passthrough_test",
    srcs = ["passthrough_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/transport_sockets/common:passthrough_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/network:transport_socket_mocks",
    ],
)
