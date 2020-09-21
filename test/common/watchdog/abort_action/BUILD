load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "abort_action_test",
    srcs = ["abort_action_test.cc"],
    external_deps = [
        "abseil_synchronization",
    ],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/registry",
        "//include/envoy/server:guarddog_config_interface",
        "//source/common/watchdog/abort_action:abort_action_lib",
        "//source/common/watchdog/abort_action:config",
        "//test/common/stats:stat_test_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/watchdog/abort_action/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:guarddog_config_interface",
        "//source/common/watchdog/abort_action:abort_action_lib",
        "//source/common/watchdog/abort_action:config",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/watchdog/abort_action/v3alpha:pkg_cc_proto",
    ],
)
