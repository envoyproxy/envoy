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

envoy_cc_test(
    name = "reverse_connection_cluster_test",
    srcs = ["reverse_connection_cluster_test.cc"],
    copts = select({
        "//bazel:windows_x86_64": [],  # TODO: fix the windows ANTLR build
        "//conditions:default": [],
    }),
    tags = ["skip_on_windows"],
    deps = [
        "//envoy/registry",
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/clusters/reverse_connection:reverse_connection_lib",
        "//source/extensions/load_balancing_policies/cluster_provided:config",
        "//test/common/upstream:utility_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/reverse_connection/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "reverse_connection_cluster_integration_test",
    size = "large",
    srcs = ["reverse_connection_cluster_integration_test.cc"],
    extension_names = [
        "envoy.clusters.reverse_connection",
        "envoy.filters.network.reverse_tunnel",
        "envoy.filters.http.lua",
        "envoy.bootstrap.reverse_tunnel.upstream_socket_interface",
        "envoy.bootstrap.reverse_tunnel.downstream_socket_interface",
        "envoy.resolvers.reverse_connection",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/common/protobuf:utility_lib",
        "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_connection_resolver_lib",
        "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_tunnel_initiator_lib",
        "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:reverse_tunnel_acceptor_lib",
        "//source/extensions/clusters/reverse_connection:reverse_connection_lib",
        "//source/extensions/filters/http/lua:config",
        "//source/extensions/filters/http/router:config",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/extensions/filters/network/reverse_tunnel:config",
        "//test/integration:http_integration_lib",
        "//test/integration:integration_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/reverse_connection/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/lua/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/reverse_tunnel/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/internal_upstream/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/http/v3:pkg_cc_proto",
    ],
)
