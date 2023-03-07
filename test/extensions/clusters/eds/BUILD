load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "eds_test",
    srcs = ["eds_test.cc"],
    deps = [
        "//source/common/config:utility_lib",
        "//source/extensions/clusters/eds:eds_lib",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/extensions/transport_sockets/tls:config",
        "//source/server:transport_socket_config_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/common/upstream:utility_lib",
        "//test/integration/load_balancers:custom_lb_policy",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:health_checker_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_benchmark_binary(
    name = "eds_speed_test",
    srcs = ["eds_speed_test.cc"],
    external_deps = [
        "benchmark",
    ],
    deps = [
        "//envoy/config:xds_resources_delegate_interface",
        "//source/common/config:grpc_mux_lib",
        "//source/common/config:grpc_subscription_lib",
        "//source/common/config:protobuf_link_hacks",
        "//source/common/config:utility_lib",
        "//source/common/config/xds_mux:grpc_mux_lib",
        "//source/extensions/clusters/eds:eds_lib",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/server:transport_socket_config_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/config:custom_config_validators_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_benchmark_test(
    name = "eds_speed_test_benchmark_test",
    benchmark_binary = "eds_speed_test",
)

envoy_cc_test(
    name = "leds_test",
    srcs = ["leds_test.cc"],
    deps = [
        "//source/common/config:utility_lib",
        "//source/extensions/clusters/eds:leds_lib",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/server:transport_socket_config_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)
