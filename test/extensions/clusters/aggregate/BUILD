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
    name = "cluster_test",
    srcs = ["cluster_test.cc"],
    extension_name = "envoy.clusters.aggregate",
    deps = [
        "//source/extensions/clusters/aggregate:cluster",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/common/upstream:utility_lib",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/mocks/upstream:load_balancer_mocks",
        "//test/test_common:environment_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/aggregate/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "cluster_update_test",
    srcs = ["cluster_update_test.cc"],
    extension_name = "envoy.clusters.aggregate",
    deps = [
        "//source/common/upstream:cluster_factory_lib",
        "//source/common/upstream:cluster_manager_lib",
        "//source/extensions/clusters/aggregate:cluster",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/common/upstream:test_cluster_manager",
        "//test/common/upstream:utility_lib",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:cluster_update_callbacks_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/aggregate/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "cluster_integration_test",
    srcs = ["cluster_integration_test.cc"],
    extension_name = "envoy.clusters.aggregate",
    deps = [
        "//source/common/config:protobuf_link_hacks",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/clusters/aggregate:cluster",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/retry/priority/previous_priorities:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/integration:http_integration_lib",
        "//test/integration:integration_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:network_utility_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)
