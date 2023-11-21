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

envoy_extension_cc_test(
    name = "internal_upstream_integration_test",
    size = "large",
    srcs = [
        "internal_upstream_integration_test.cc",
    ],
    extension_names = [
        "envoy.bootstrap.internal_listener",
        "envoy.filters.http.set_filter_state",
    ],
    deps = [
        "//source/common/network:connection_lib",
        "//source/common/network:utility_lib",
        "//source/common/router:string_accessor_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/bootstrap/internal_listener:config",
        "//source/extensions/filters/http/set_filter_state:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/io_socket/user_space:config",
        "//source/extensions/transport_sockets/internal_upstream:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/bootstrap/internal_listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/internal_upstream/v3:pkg_cc_proto",
    ],
)
