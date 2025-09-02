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
    extension_names = ["envoy.clusters.composite"],
    deps = [
        "//source/common/singleton:manager_impl_lib",
        "//source/common/upstream:cluster_factory_lib",
        "//source/extensions/clusters/composite:cluster_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/mocks/upstream:thread_local_cluster_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/clusters/composite/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "cluster_integration_test",
    size = "large",
    srcs = ["cluster_integration_test.cc"],
    extension_names = ["envoy.clusters.composite"],
    deps = [
        "//source/common/config:protobuf_link_hacks",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/clusters/composite:cluster_lib",
        "//test/config:v2_link_hacks",
        "//test/integration:http_integration_lib",
        "//test/integration:integration_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:network_utility_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/composite/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)
