load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "dns_srv_cluster_integration_test",
    srcs = ["dns_srv_cluster_integration_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/clusters/dns_srv:cluster",
        "//test/integration:http_integration_lib",
        "//test/mocks/network:network_mocks",
        "//test/test_common:registry_lib",
        "@envoy_api//envoy/extensions/clusters/dns_srv/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "dns_srv_cluster_test",
    srcs = ["dns_srv_cluster_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/config:utility_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/clusters/dns_srv:cluster",
        "//source/extensions/load_balancing_policies/round_robin:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/common/upstream:utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/dns_srv/v3:pkg_cc_proto",
    ],
)
