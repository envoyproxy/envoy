load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "connection_mocks",
    srcs = ["connection.cc"],
    hdrs = ["connection.h"],
    deps = [
        "//include/envoy/network:connection_interface",
        "//source/common/network:filter_manager_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
    ],
)

envoy_cc_mock(
    name = "io_handle_mocks",
    srcs = ["io_handle.cc"],
    hdrs = ["io_handle.h"],
    deps = [
        "//include/envoy/network:io_handle_interface",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_mock(
    name = "network_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        ":connection_mocks",
        ":transport_socket_mocks",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:drain_decision_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/network:resolver_interface",
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/server:listener_manager_interface",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:printers_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_mock(
    name = "transport_socket_mocks",
    srcs = ["transport_socket.cc"],
    hdrs = ["transport_socket.h"],
    deps = [
        "//include/envoy/network:transport_socket_interface",
        "//source/common/network:utility_lib",
    ],
)
