load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "reverse_tunnel_acceptor_test",
    size = "large",
    srcs = ["reverse_tunnel_acceptor_test.cc"],
    extension_names = ["envoy.bootstrap.reverse_connection.upstream_reverse_connection_socket_interface"],
    deps = [
        "//source/common/network:socket_interface_lib",
        "//source/common/thread_local:thread_local_lib",
        "//source/extensions/bootstrap/reverse_tunnel:reverse_tunnel_acceptor_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/bootstrap/reverse_connection_socket_interface/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "reverse_connection_utility_test",
    size = "medium",
    srcs = ["reverse_connection_utility_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/network:connection_lib",
        "//source/extensions/bootstrap/reverse_connection_socket_interface:reverse_connection_utility_lib",
        "//test/mocks/network:network_mocks",
        "//test/test_common:test_runtime_lib",
    ],
)
