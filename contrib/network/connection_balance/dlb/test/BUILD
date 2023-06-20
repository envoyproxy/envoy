load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//contrib/network/connection_balance/dlb/source:connection_balancer",
        "//source/common/protobuf:utility_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:status_utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
