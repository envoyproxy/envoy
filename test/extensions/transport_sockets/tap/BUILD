load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_select_admin_functionality",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "tap_config_impl_test",
    srcs = ["tap_config_impl_test.cc"],
    extension_names = ["envoy.transport_sockets.tap"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/transport_sockets/tap:tap_config_impl",
        "//test/extensions/common/tap:common",
        "//test/mocks/network:network_mocks",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_extension_cc_test(
    name = "ssl_tap_integration_test",
    srcs = envoy_select_admin_functionality(["ssl_tap_integration_test.cc"]),
    data = [
        "//test/config/integration/certs",
    ],
    extension_names = ["envoy.transport_sockets.tap"],
    rbe_pool = "2core",
    deps = [
        "//source/common/network:connection_lib",
        "//source/extensions/transport_sockets/tap:config",
        "//source/extensions/transport_sockets/tls:config",
        "//test/common/tls/integration:ssl_integration_test_lib",
        "//test/extensions/common/tap:common",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)
