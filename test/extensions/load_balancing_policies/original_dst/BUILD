load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.load_balancing_policies.original_dst"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/load_balancing_policies/original_dst:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/original_dst/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "original_dst_lb_test",
    srcs = ["original_dst_lb_test.cc"],
    extension_names = ["envoy.load_balancing_policies.original_dst"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:filter_state_dst_address_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/clusters/original_dst:original_dst_cluster_lib",
        "//source/extensions/load_balancing_policies/cluster_provided:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/common/upstream:utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "original_dst_integration_test",
    size = "large",
    srcs = ["original_dst_integration_test.cc"],
    extension_names = ["envoy.load_balancing_policies.original_dst"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/protobuf",
        "//source/extensions/clusters/original_dst:original_dst_cluster_lib",
        "//source/extensions/load_balancing_policies/original_dst:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_cc_benchmark_binary(
    name = "original_dst_lb_benchmark",
    srcs = ["original_dst_lb_benchmark.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/clusters/original_dst:original_dst_cluster_lib",
        "//source/extensions/load_balancing_policies/cluster_provided:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/extensions/load_balancing_policies/common:benchmark_base_tester_lib",
        "//test/mocks:common_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_benchmark_test(
    name = "original_dst_lb_benchmark_test",
    timeout = "long",
    benchmark_binary = "original_dst_lb_benchmark",
)
