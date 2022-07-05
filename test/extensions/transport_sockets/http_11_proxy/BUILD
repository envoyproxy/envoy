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
    name = "connect_test",
    srcs = ["connect_test.cc"],
    extension_names = ["envoy.transport_sockets.http_11_proxy"],
    deps = [
        "//source/extensions/transport_sockets/http_11_proxy:connect",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:io_handle_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/network:transport_socket_mocks",
    ],
)

envoy_extension_cc_test(
    name = "connect_integration_test",
    srcs = ["connect_integration_test.cc"],
    extension_names = ["envoy.transport_sockets.http_11_proxy"],
    deps = [
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/transport_sockets/http_11_proxy:upstream_config",
        "//test/integration:http_integration_lib",
        "//test/integration:integration_lib",
        "//test/integration/filters:header_to_proxy_filter_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/http_11_proxy/v3:pkg_cc_proto",
    ],
)
