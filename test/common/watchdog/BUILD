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
    rbe_pool = "2core",
    deps = [
        "//envoy/common:time_interface",
        "//envoy/registry",
        "//envoy/server:guarddog_config_interface",
        "//source/common/watchdog:abort_action_config",
        "//source/common/watchdog:abort_action_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/synchronization",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/watchdog/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "abort_action_config_test",
    srcs = ["abort_action_config_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//envoy/registry",
        "//envoy/server:guarddog_config_interface",
        "//source/common/watchdog:abort_action_config",
        "//source/common/watchdog:abort_action_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/watchdog/v3:pkg_cc_proto",
    ],
)
