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
    name = "health_checker_impl_test",
    srcs = ["health_checker_impl_test.cc"],
    extension_names = ["envoy.health_checkers.udp"],
    deps = [
        "//envoy/registry",
        "//source/common/network:io_socket_error_lib",
        "//source/common/upstream:health_checker_lib",
        "//source/extensions/health_checkers/udp:health_checker_lib",
        "//test/common/upstream:health_checker_test_base_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/network:socket_mocks",
        "//test/mocks/server:health_checker_factory_context_mocks",
        "//test/test_common:simulated_time_system_lib",
    ],
)
