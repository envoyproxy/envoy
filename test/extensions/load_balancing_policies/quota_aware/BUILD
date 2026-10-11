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
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = [
        "envoy.load_balancing_policies.quota_aware",
        "envoy.load_balancing_policies.round_robin",
    ],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/config:utility_lib",
        "//source/extensions/load_balancing_policies/quota_aware:config",
        "//source/extensions/load_balancing_policies/round_robin:config",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/quota_aware/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/round_robin/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "load_balancer_test",
    srcs = ["load_balancer_test.cc"],
    extension_names = [
        "envoy.load_balancing_policies.quota_aware",
        "envoy.load_balancing_policies.round_robin",
    ],
    rbe_pool = "linux_x64_small",
    deps = [
        "//envoy/http:codes_interface",
        "//source/common/config:metadata_lib",
        "//source/extensions/load_balancing_policies/quota_aware:config",
        "//source/extensions/load_balancing_policies/quota_aware:load_balancer",
        "//source/extensions/load_balancing_policies/round_robin:config",
        "//test/common/upstream:utility_lib",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/quota_aware/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/round_robin/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    extension_names = [
        "envoy.load_balancing_policies.quota_aware",
        "envoy.load_balancing_policies.round_robin",
        "envoy.filters.http.set_metadata",
    ],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/filters/http/set_metadata:config",
        "//source/extensions/load_balancing_policies/quota_aware:config",
        "//source/extensions/load_balancing_policies/round_robin:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)
