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
    name = "internal_upstream_test",
    srcs = ["internal_upstream_test.cc"],
    extension_names = ["envoy.transport_sockets.internal_upstream"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:proxy_protocol_filter_state_lib",
        "//source/extensions/io_socket/user_space:io_handle_lib",
        "//source/extensions/transport_sockets/internal_upstream",
        "//test/mocks/network:io_handle_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/network:transport_socket_mocks",
    ],
)
