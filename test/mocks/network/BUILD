licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

envoy_package()

envoy_cc_mock(
    name = "network_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:drain_decision_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/server:listener_manager_interface",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//test/mocks/event:event_mocks",
    ],
)
