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
    size = "medium",
    srcs = ["reverse_tunnel_acceptor_test.cc"],
    extension_names = ["envoy.bootstrap.reverse_tunnel.upstream_socket_interface"],
    deps = [
        "//source/common/network:utility_lib",
        "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:reverse_tunnel_acceptor_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "@envoy_api//envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "reverse_tunnel_acceptor_extension_test",
    size = "medium",
    srcs = ["reverse_tunnel_acceptor_extension_test.cc"],
    deps = [
        "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:reverse_tunnel_acceptor_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/reverse_tunnel_reporting_service:reporter_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:registry_lib",
        "@envoy_api//envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "upstream_socket_manager_test",
    size = "large",
    srcs = ["upstream_socket_manager_test.cc"],
    deps = [
        "//source/common/network:utility_lib",
        "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:reverse_tunnel_acceptor_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/reverse_tunnel_reporting_service:reporter_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:registry_lib",
        "@envoy_api//envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "upstream_reverse_connection_io_handle_test",
    size = "medium",
    srcs = ["upstream_reverse_connection_io_handle_test.cc"],
    deps = [
        "//source/common/network:utility_lib",
        "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:reverse_tunnel_acceptor_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:registry_lib",
    ],
)

envoy_cc_test(
    name = "config_validation_test",
    size = "small",
    srcs = ["config_validation_test.cc"],
    deps = [
        "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:reverse_tunnel_acceptor_lib",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3:pkg_cc_proto",
    ],
)
