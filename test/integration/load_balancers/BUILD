load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "custom_lb_policy",
    srcs = [
        "custom_lb_policy.cc",
    ],
    hdrs = [
        "custom_lb_policy.h",
    ],
    deps = [
        ":config_cc_proto",
        "//envoy/upstream:load_balancer_interface",
        "//source/common/upstream:load_balancer_factory_base_lib",
        "//test/test_common:registry_lib",
    ],
)

envoy_proto_library(
    name = "config",
    srcs = [":config.proto"],
)

envoy_cc_test(
    name = "deterministic_aperture_lb_policy_test",
    srcs = [
        "deterministic_aperture_lb_policy_test.cc",
    ],
    deps = [
        ":config_cc_proto",
        "//envoy/upstream:load_balancer_interface",
        "//source/common/upstream:deterministic_aperture_lb_lib",
        "//source/common/upstream:load_balancer_factory_base_lib",
        "//test/common/upstream:utility_lib",
        "//test/integration:http_integration_lib",
        "//test/mocks:common_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/mocks/upstream:load_balancer_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:simulated_time_system_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/deterministic_aperture/v3:pkg_cc_proto",
    ],
)
