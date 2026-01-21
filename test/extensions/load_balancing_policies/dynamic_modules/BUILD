load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:lb_callbacks_test",
        "//test/extensions/dynamic_modules/test_data/c:lb_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:lb_invalid_host_index",
        "//test/extensions/dynamic_modules/test_data/c:lb_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:lb_no_choose_host",
        "//test/extensions/dynamic_modules/test_data/c:lb_round_robin",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/load_balancing_policies/dynamic_modules:config",
        "//source/extensions/load_balancing_policies/dynamic_modules:load_balancer_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/load_balancing_policies/dynamic_modules/v3:pkg_cc_proto",
    ],
)
