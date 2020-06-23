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
    name = "tap_config_impl_test",
    srcs = ["tap_config_impl_test.cc"],
    extension_name = "envoy.transport_sockets.tap",
    deps = [
        "//source/extensions/transport_sockets/tap:tap_config_impl",
        "//test/extensions/common/tap:common",
        "//test/mocks/network:network_mocks",
        "//test/test_common:simulated_time_system_lib",
    ],
)
