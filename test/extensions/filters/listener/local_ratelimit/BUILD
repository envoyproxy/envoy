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
    name = "local_ratelimit_test",
    srcs = ["local_ratelimit_test.cc"],
    extension_names = ["envoy.filters.listener.local_ratelimit"],
    rbe_pool = "2core",
    deps = [
        "//source/common/network:default_socket_interface_lib",
        "//source/common/network:listener_filter_buffer_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/listener/local_ratelimit:config",
        "//source/extensions/filters/listener/local_ratelimit:local_ratelimit_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
    ],
)

envoy_extension_cc_test(
    name = "local_ratelimit_integration_test",
    size = "large",
    srcs = [
        "local_ratelimit_integration_test.cc",
    ],
    extension_names = ["envoy.filters.listener.local_ratelimit"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/filters/listener/local_ratelimit:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/integration:base_integration_test_lib",
        "//test/integration:common_extensions_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/listener/local_ratelimit/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)
