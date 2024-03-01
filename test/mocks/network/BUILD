load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
    "envoy_select_enable_http3",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "connection_mocks",
    srcs = ["connection.cc"],
    hdrs = ["connection.h"],
    deps = [
        "//envoy/network:connection_interface",
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
        "//envoy/event:dispatcher_interface",
        "//envoy/network:io_handle_interface",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_mock(
    name = "socket_mocks",
    srcs = ["socket.cc"],
    hdrs = ["socket.h"],
    deps = [
        ":io_handle_mocks",
        "//envoy/network:socket_interface",
    ],
)

envoy_cc_mock(
    name = "network_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        ":connection_mocks",
        ":io_handle_mocks",
        ":transport_socket_mocks",
        "//envoy/buffer:buffer_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:drain_decision_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:resolver_interface",
        "//envoy/network:transport_socket_interface",
        "//envoy/server:listener_manager_interface",
        "//source/common/network:address_lib",
        "//source/common/network:listener_lib",
        "//source/common/network:socket_interface_lib",
        "//source/common/network:utility_lib",
        "//source/common/network/dns_resolver:dns_factory_util_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:printers_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ] + envoy_select_enable_http3([
        "@com_github_google_quiche//:quic_platform_socket_address",
    ]),
)

envoy_cc_mock(
    name = "transport_socket_mocks",
    srcs = ["transport_socket.cc"],
    hdrs = ["transport_socket.h"],
    deps = [
        "//envoy/network:io_handle_interface",
        "//envoy/network:transport_socket_interface",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:utility_lib",
    ],
)
