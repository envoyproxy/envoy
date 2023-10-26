load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_benchmark_test",
    "envoy_extension_cc_benchmark_binary",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.load_balancing_policies.subset"],
    deps = [
        "//source/extensions/load_balancing_policies/random:config",
        "//source/extensions/load_balancing_policies/subset:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/subset/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    extension_names = ["envoy.load_balancing_policies.subset"],
    deps = [
        "//source/common/protobuf",
        "//source/extensions/filters/http/header_to_metadata:config",
        "//source/extensions/load_balancing_policies/random:config",
        "//source/extensions/load_balancing_policies/subset:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "subset_test",
    srcs = ["subset_test.cc"],
    extension_names = ["envoy.load_balancing_policies.subset"],
    deps = [
        "//source/common/common:minimal_logger_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/load_balancing_policies/subset:config",
        "//test/common/upstream:utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/mocks/upstream:load_balancer_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/test_common:simulated_time_system_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_benchmark_binary(
    name = "subset_benchmark",
    srcs = ["subset_benchmark.cc"],
    extension_names = ["envoy.load_balancing_policies.subset"],
    external_deps = [
        "benchmark",
    ],
    deps = [
        "//source/extensions/load_balancing_policies/subset:config",
        "//test/extensions/load_balancing_policies/common:benchmark_base_tester_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_extension_benchmark_test(
    name = "subset_benchmark_test",
    timeout = "long",
    benchmark_binary = "subset_benchmark",
    extension_names = ["envoy.load_balancing_policies.subset"],
)
