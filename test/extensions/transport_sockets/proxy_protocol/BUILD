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
    extension_names = ["envoy.transport_sockets.upstream_proxy_protocol"],
    rbe_pool = "2core",
    deps = [
        "//envoy/network:proxy_protocol_options_lib",
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
    size = "large",
    srcs = ["proxy_protocol_integration_test.cc"],
    extension_names = ["envoy.transport_sockets.upstream_proxy_protocol"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/listener/proxy_protocol:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/health_checkers/http:health_checker_lib",
        "//source/extensions/health_checkers/tcp:health_checker_lib",
        "//source/extensions/transport_sockets/proxy_protocol:upstream_config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/integration:http_integration_lib",
        "//test/integration:integration_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/listener/proxy_protocol/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/proxy_protocol/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/raw_buffer/v3:pkg_cc_proto",
    ],
)
