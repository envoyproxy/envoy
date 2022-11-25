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
    name = "lb_policy_test",
    srcs = [
        "lb_policy_test.cc",
    ],
    extension_names = ["envoy.load_balancing_policies.deterministic_aperture"],
    deps = [
        "//envoy/upstream:load_balancer_interface",
        "//source/common/upstream:load_balancer_factory_base_lib",
        "//source/extensions/load_balancing_policies/deterministic_aperture:load_balancer_lib",
        "//test/common/upstream:utility_lib",
        "//test/integration:http_integration_lib",
        "//test/mocks:common_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/mocks/upstream:load_balancer_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/test_common:simulated_time_system_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/deterministic_aperture/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    srcs = [
        "integration_test.cc",
    ],
    extension_names = ["envoy.load_balancing_policies.deterministic_aperture"],
    deps = [
        "//envoy/upstream:load_balancer_interface",
        "//source/common/upstream:load_balancer_factory_base_lib",
        "//source/extensions/load_balancing_policies/deterministic_aperture:load_balancer_lib",
        "//test/common/upstream:utility_lib",
        "//test/integration:http_integration_lib",
        "//test/mocks:common_lib",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/mocks/upstream:load_balancer_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/test_common:simulated_time_system_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/deterministic_aperture/v3:pkg_cc_proto",
    ],
)
