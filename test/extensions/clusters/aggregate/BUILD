licenses(["notice"])  # Apache 2

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

envoy_package()

envoy_extension_cc_test(
    name = "cluster_test",
    srcs = ["cluster_test.cc"],
    extension_name = "envoy.clusters.aggregate",
    deps = [
        "//source/extensions/clusters/aggregate:cluster",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/common/upstream:utility_lib",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:environment_lib",
    ],
)

envoy_extension_cc_test(
    name = "cluster_integration_test",
    srcs = ["cluster_integration_test.cc"],
    extension_name = "envoy.clusters.aggregate",
    deps = [
        "//source/common/upstream:cluster_factory_lib",
        "//source/common/upstream:cluster_manager_lib",
        "//source/extensions/clusters/aggregate:cluster",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/common/upstream:test_cluster_manager",
        "//test/common/upstream:utility_lib",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)
