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
        "//source/common/network:socket_interface_lib",
        "//source/extensions/bootstrap/reverse_tunnel/common:reverse_connection_utility_lib",
        "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:reverse_tunnel_acceptor_lib",
        "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:upstream_socket_manager_lib",
        "//test/common/tls:mock_ssl_handshaker_lib",
        "//test/mocks:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test(
    name = "rping_interceptor_test",
    size = "medium",
    srcs = ["rping_interceptor_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/bootstrap/reverse_tunnel/common:reverse_connection_utility_lib",
        "//source/extensions/bootstrap/reverse_tunnel/common:rping_interceptor_lib",
        "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:reverse_tunnel_acceptor_lib",
        "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:upstream_socket_manager_lib",
    ],
)
