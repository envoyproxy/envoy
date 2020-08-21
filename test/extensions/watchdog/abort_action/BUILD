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
    name = "abort_action_test",
    srcs = ["abort_action_test.cc"],
    extension_name = "envoy.watchdog.abort_action",
    external_deps = [
        "abseil_synchronization",
    ],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/registry",
        "//include/envoy/server:guarddog_config_interface",
        "//source/extensions/watchdog/abort_action:abort_action_lib",
        "//source/extensions/watchdog/abort_action:config",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/watchdog/abort_action/v3alpha:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.watchdog.abort_action",
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:guarddog_config_interface",
        "//source/extensions/watchdog/abort_action:abort_action_lib",
        "//source/extensions/watchdog/abort_action:config",
        "//test/mocks/event:event_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/watchdog/abort_action/v3alpha:pkg_cc_proto",
    ],
)
