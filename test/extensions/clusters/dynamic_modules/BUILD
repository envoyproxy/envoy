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
        "//test/extensions/dynamic_modules/test_data/c:cluster_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:cluster_no_op",
        "//test/extensions/dynamic_modules/test_data/c:no_op",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/clusters/dynamic_modules:cluster_lib",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/load_balancing_policies/cluster_provided:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/common/upstream:utility_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/dynamic_modules/v3:pkg_cc_proto",
    ],
)
