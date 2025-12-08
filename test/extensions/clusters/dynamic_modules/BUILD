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
    data = [
        "//test/extensions/dynamic_modules/test_data/c:cluster_no_op",
        "//test/extensions/dynamic_modules/test_data/c:program_init_fail",
    ],
    extension_names = ["envoy.clusters.dynamic_modules"],
    deps = [
        "//source/extensions/clusters/dynamic_modules:cluster_lib",
        "//source/extensions/clusters/dynamic_modules:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:priority_set_mocks",
    ],
)

envoy_extension_cc_test(
    name = "cluster_config_test",
    srcs = ["cluster_config_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:cluster_no_op",
    ],
    extension_names = ["envoy.clusters.dynamic_modules"],
    deps = [
        "//source/extensions/clusters/dynamic_modules:cluster_lib",
        "//source/extensions/clusters/dynamic_modules:config",
        "//source/extensions/load_balancing_policies/cluster_provided:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "abi_impl_test",
    srcs = ["abi_impl_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:cluster_no_op",
    ],
    extension_names = ["envoy.clusters.dynamic_modules"],
    deps = [
        "//source/extensions/clusters/dynamic_modules:cluster_lib",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/http:stream_mock",
        "//test/mocks/network:connection_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/mocks/upstream:thread_local_cluster_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:cluster_test",
    ],
    extension_names = ["envoy.clusters.dynamic_modules"],
    deps = [
        "//source/extensions/clusters/dynamic_modules:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/dynamic_modules/v3:pkg_cc_proto",
    ],
)
