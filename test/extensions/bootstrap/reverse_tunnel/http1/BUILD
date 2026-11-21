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
    name = "http1_handshake_initiator_test",
    size = "medium",
    srcs = ["http1_handshake_initiator_test.cc"],
    extension_names = ["envoy.bootstrap.reverse_tunnel.downstream_socket_interface"],
    rbe_pool = {"build": "linux_x64_small"},
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/http:header_map_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/bootstrap/reverse_tunnel/http1:http1_handshake_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
