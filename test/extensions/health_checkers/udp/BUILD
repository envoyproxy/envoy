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
        "//source/common/network:utility_lib",
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

envoy_extension_cc_test(
    name = "health_checker_integration_test",
    size = "large",
    srcs = ["health_checker_integration_test.cc"],
    extension_names = ["envoy.health_checkers.udp"],
    rbe_pool = "linux_x64_small",
    tags = ["cpu:3"],
    deps = [
        "//source/extensions/health_checkers/udp:health_checker_lib",
        "//test/config:v2_link_hacks",
        "//test/integration:http_integration_lib",
        "//test/integration:integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/health_checkers/udp/v3:pkg_cc_proto",
    ],
)
