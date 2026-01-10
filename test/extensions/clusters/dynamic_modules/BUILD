load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "cluster_test",
    srcs = ["cluster_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:cluster_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:cluster_lb_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:cluster_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:cluster_no_op",
        "//test/extensions/dynamic_modules/test_data/c:no_op",
        "//test/extensions/dynamic_modules/test_data/rust:cluster_test",
    ],
    deps = [
        "//source/extensions/clusters/dynamic_modules:cluster_config_lib",
        "//source/extensions/clusters/dynamic_modules:cluster_lib",
        "//source/extensions/clusters/dynamic_modules:config",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/load_balancing_policies/cluster_provided:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/common/upstream:utility_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/server:options_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:cluster_no_op",
        "//test/extensions/dynamic_modules/test_data/rust:cluster_test",
    ],
    deps = [
        "//source/extensions/clusters/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/clusters/dynamic_modules/v3:pkg_cc_proto",
    ],
)
