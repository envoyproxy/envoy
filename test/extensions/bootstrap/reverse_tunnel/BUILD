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
    name = "reverse_tunnel_initiator_test",
    size = "large",
    srcs = ["reverse_tunnel_initiator_test.cc"],
    extension_names = ["envoy.bootstrap.reverse_tunnel.downstream_socket_interface"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:socket_interface_lib",
        "//source/common/network:utility_lib",
        "//source/common/thread_local:thread_local_lib",
        "//source/extensions/bootstrap/reverse_tunnel:reverse_connection_handshake_cc_proto",
        "//source/extensions/bootstrap/reverse_tunnel:reverse_tunnel_initiator_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "reverse_connection_address_test",
    size = "medium",
    srcs = ["reverse_connection_address_test.cc"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:default_socket_interface_lib",
        "//source/common/singleton:threadsafe_singleton",
        "//source/extensions/bootstrap/reverse_tunnel:reverse_connection_address_lib",
        "//test/mocks/network:network_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test(
    name = "reverse_connection_resolver_test",
    size = "medium",
    srcs = ["reverse_connection_resolver_test.cc"],
    deps = [
        "//source/common/network:address_lib",
        "//source/extensions/bootstrap/reverse_tunnel:reverse_connection_resolver_lib",
        "//test/mocks/network:network_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
