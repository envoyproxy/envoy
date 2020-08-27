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
    name = "profile_action_test",
    srcs = ["profile_action_test.cc"],
    extension_name = "envoy.watchdog.profile_action",
    external_deps = [
        "abseil_synchronization",
    ],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/registry",
        "//include/envoy/server:guarddog_config_interface",
        "//source/common/filesystem:directory_lib",
        "//source/common/profiler:profiler_lib",
        "//source/extensions/watchdog/profile_action:config",
        "//source/extensions/watchdog/profile_action:profile_action_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/watchdog/profile_action/v3alpha:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.watchdog.profile_action",
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:guarddog_config_interface",
        "//source/extensions/watchdog/profile_action:config",
        "//source/extensions/watchdog/profile_action:profile_action_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/watchdog/profile_action/v3alpha:pkg_cc_proto",
    ],
)
