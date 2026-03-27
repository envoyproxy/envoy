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
        "//source/extensions/clusters/composite:cluster",
        "//source/extensions/load_balancing_policies/cluster_provided:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/common/upstream:utility_lib",
        "//test/mocks/http:conn_pool_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:connection_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/mocks/upstream:load_balancer_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/mocks/upstream:thread_local_cluster_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "@envoy_api//envoy/extensions/clusters/composite/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "cluster_integration_test",
    size = "large",
    srcs = ["cluster_integration_test.cc"],
    extension_names = ["envoy.clusters.composite"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/config:protobuf_link_hacks",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/clusters/composite:cluster",
        "//source/extensions/load_balancing_policies/cluster_provided:config",
        "//test/config:v2_link_hacks",
        "//test/integration:http_integration_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/composite/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)
