load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.network.reverse_tunnel"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/network/reverse_tunnel:config",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "filter_unit_test",
    srcs = ["filter_unit_test.cc"],
    extension_names = ["envoy.filters.network.reverse_tunnel"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/stats:isolated_store_lib",
        "//source/common/stream_info:uint64_accessor_lib",
        "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:reverse_tunnel_acceptor_lib",
        "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:upstream_socket_manager_lib",
        "//source/extensions/filters/network/reverse_tunnel:reverse_tunnel_filter_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:overload_manager_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:logging_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/reverse_tunnel/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    extension_names = [
        "envoy.filters.network.reverse_tunnel",
        "envoy.bootstrap.reverse_tunnel.upstream_socket_interface",
        "envoy.bootstrap.reverse_tunnel.downstream_socket_interface",
        "envoy.bootstrap.internal_listener",
        "envoy.resolvers.reverse_connection",
        "envoy.filters.network.echo",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/bootstrap/internal_listener:config",
        "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_connection_resolver_lib",
        "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_tunnel_initiator_lib",
        "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:reverse_tunnel_acceptor_lib",
        "//source/extensions/filters/network/echo:config",
        "//source/extensions/filters/network/reverse_tunnel:config",
        "//source/extensions/filters/network/set_filter_state:config",
        "//source/extensions/transport_sockets/internal_upstream:config",
        "//test/integration:integration_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/reverse_tunnel/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/internal_upstream/v3:pkg_cc_proto",
    ],
)
