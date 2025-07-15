load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test",
    "envoy_package",
)

load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "reverse_connection_cluster_test",
    srcs = ["reverse_connection_cluster_test.cc"],
    deps = [
        "//envoy/registry",
        "//source/extensions/clusters/reverse_connection:reverse_connection_lib",
        "//source/extensions/load_balancing_policies/cluster_provided:config",
        "//test/common/upstream:utility_lib",
        "//test/test_common:registry_lib",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:utility_lib",
    ],
) 