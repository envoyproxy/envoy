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
    # This makes the test targets dependent on this target only run on the desired platform.
    # The actual feature is dependent on hardware but this mock library can run on other platforms.
    target_compatible_with = envoy_contrib_linux_x86_64_constraints(),
    deps = [
        "//contrib/dlb/source:connection_balancer",
        "//source/common/protobuf:utility_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:status_utility_lib",
        "@envoy_api//contrib/envoy/extensions/network/connection_balance/dlb/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
