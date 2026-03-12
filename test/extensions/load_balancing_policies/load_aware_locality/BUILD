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
    extension_names = ["envoy.load_balancing_policies.load_aware_locality"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/load_balancing_policies/cluster_provided:config",
        "//source/extensions/load_balancing_policies/load_aware_locality:config",
        "//source/extensions/load_balancing_policies/maglev:config",
        "//source/extensions/load_balancing_policies/round_robin:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/cluster_provided/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/maglev/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "load_aware_locality_lb_test",
    srcs = ["load_aware_locality_lb_test.cc"],
    extension_names = ["envoy.load_balancing_policies.load_aware_locality"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/load_balancing_policies/load_aware_locality:config",
        "//source/extensions/load_balancing_policies/load_aware_locality:load_aware_locality_lb_lib",
        "//source/extensions/load_balancing_policies/round_robin:config",
        "//test/common/upstream:utility_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "@abseil-cpp//absl/container:flat_hash_map",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    extension_names = ["envoy.load_balancing_policies.load_aware_locality"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/protobuf",
        "//source/extensions/load_balancing_policies/load_aware_locality:config",
        "//source/extensions/load_balancing_policies/round_robin:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@xds//xds/data/orca/v3:pkg_cc_proto",
    ],
)
