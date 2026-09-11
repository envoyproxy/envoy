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
    name = "multi_test",
    srcs = ["multi_test.cc"],
    extension_names = ["envoy.health_checkers.multi"],
    deps = [
        "//source/extensions/health_checkers/http:health_checker_lib",
        "//source/extensions/health_checkers/multi:health_checker_lib",
        "//source/extensions/health_checkers/tcp:health_checker_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:health_checker_factory_context_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/upstream:cluster_priority_set_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_extension_cc_test(
    name = "multi_integration_test",
    size = "large",
    srcs = ["multi_integration_test.cc"],
    extension_names = [
        "envoy.health_checkers.multi",
        "envoy.health_checkers.tcp",
    ],
    rbe_pool = "linux_x64_small",
    tags = ["cpu:3"],
    deps = [
        "//source/extensions/health_checkers/multi:health_checker_lib",
        "//source/extensions/health_checkers/tcp:health_checker_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:simulated_time_system_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/health_checkers/multi/v3:pkg_cc_proto",
    ],
)
