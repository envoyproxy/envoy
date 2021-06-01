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
    name = "proxy_protocol_test",
    srcs = ["proxy_protocol_test.cc"],
    extension_name = "envoy.transport_sockets.upstream_proxy_protocol",
    deps = [
        "//include/envoy/network:proxy_protocol_options_lib",
        "//source/extensions/common/proxy_protocol:proxy_protocol_header_lib",
        "//source/extensions/transport_sockets/proxy_protocol:upstream_proxy_protocol",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:io_handle_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/network:transport_socket_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "proxy_protocol_integration_test",
    srcs = ["proxy_protocol_integration_test.cc"],
    extension_name = "envoy.transport_sockets.upstream_proxy_protocol",
    deps = [
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/transport_sockets/proxy_protocol:upstream_config",
        "//test/integration:integration_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/proxy_protocol/v3:pkg_cc_proto",
    ],
)
