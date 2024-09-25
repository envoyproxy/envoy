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
    name = "tcp_stats_test",
    srcs = ["tcp_stats_test.cc"],
    extension_names = ["envoy.transport_sockets.tcp_stats"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/extensions/transport_sockets/tcp_stats:config",
        "//source/extensions/transport_sockets/tcp_stats:tcp_stats_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:io_handle_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/network:transport_socket_mocks",
        "//test/mocks/server:transport_socket_factory_context_mocks",
        "@envoy_api//envoy/extensions/transport_sockets/raw_buffer/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tcp_stats/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "tcp_stats_integration_test",
    size = "large",
    srcs = ["tcp_stats_integration_test.cc"],
    extension_names = ["envoy.transport_sockets.tcp_stats"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/extensions/transport_sockets/tcp_stats:config",
        "//test/integration:http_integration_lib",
        "//test/integration:integration_lib",
        "@envoy_api//envoy/extensions/transport_sockets/raw_buffer/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tcp_stats/v3:pkg_cc_proto",
    ],
)
