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
        "//source/common/network:utility_lib",
        "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_connection_address_lib",
        "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_connection_io_handle_lib",
        "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_tunnel_initiator_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:logging_lib",
        "@envoy_api//envoy/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "reverse_tunnel_initiator_extension_test",
    size = "medium",
    srcs = ["reverse_tunnel_initiator_extension_test.cc"],
    deps = [
        "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_tunnel_extension_lib",
        "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_tunnel_initiator_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//envoy/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "reverse_connection_io_handle_test",
    size = "large",
    srcs = ["reverse_connection_io_handle_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_connection_io_handle_lib",
        "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_tunnel_extension_lib",
        "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_tunnel_initiator_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
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
        "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_connection_address_lib",
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
        "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_connection_resolver_lib",
        "//test/mocks/network:network_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "rc_connection_wrapper_test",
    size = "large",
    srcs = ["rc_connection_wrapper_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_connection_io_handle_lib",
        "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_tunnel_extension_lib",
        "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_tunnel_initiator_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//envoy/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/v3:pkg_cc_proto",
    ],
)
