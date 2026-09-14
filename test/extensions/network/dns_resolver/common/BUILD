load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "fake_udp_dns_server_lib",
    srcs = ["fake_udp_dns_server.cc"],
    hdrs = ["fake_udp_dns_server.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:io_handle_interface",
        "//envoy/network:socket_interface",
        "//source/common/common:assert_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:utility_lib",
    ],
)
