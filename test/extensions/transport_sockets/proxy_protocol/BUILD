licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "proxy_protocol_test",
    srcs = ["proxy_protocol_test.cc"],
    extension_name = "envoy.transport_sockets.proxy_protocol",
    deps = [
        "//source/extensions/common/proxy_protocol:proxy_protocol_header_lib",
        "//source/extensions/transport_sockets/proxy_protocol:proxy_protocol",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:io_handle_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/network:transport_socket_mocks",
    ],
)
