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
        "//envoy/common:hashable_interface",
        "//source/common/stream_info:filter_state_lib",
        "//source/extensions/io_socket/user_space:io_handle_lib",
        "//source/extensions/transport_sockets/internal_upstream",
        "//source/extensions/transport_sockets/internal_upstream:config",
        "//test/mocks/network:io_handle_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/network:transport_socket_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/internal_upstream/v3:pkg_cc_proto",
    ],
)
