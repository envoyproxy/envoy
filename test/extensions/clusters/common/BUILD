load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "logical_host_test",
    srcs = ["logical_host_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/clusters/common:logical_host_lib",
        "//test/mocks/network:transport_socket_mocks",
        "//test/mocks/upstream:host_mocks",
    ],
)

envoy_cc_test(
    name = "logical_host_integration_test",
    srcs = ["logical_host_integration_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/config:utility_lib",
        "//source/extensions/clusters/common:logical_host_lib",
        "//source/extensions/clusters/logical_dns:logical_dns_cluster_lib",
        "//test/integration:http_integration_lib",
        "//test/mocks/network:network_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)
