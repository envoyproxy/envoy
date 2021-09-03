load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "socket_interface_integration_test",
    srcs = ["socket_interface_integration_test.cc"],
    deps = [
        "//source/extensions/filters/network/echo:config",
        "//source/extensions/io_socket/io_uring:config",
        "//test/integration:http_integration_lib",
    ],
)
