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
    name = "backtrace_action_test",
    srcs = ["backtrace_action_test.cc"],
    extension_names = ["envoy.watchdog.backtrace_action"],
    rbe_pool = "6gig",
    tags = ["skip_on_windows"],
    deps = [
        "//envoy/common:time_interface",
        "//envoy/server:guarddog_config_interface",
        "//source/common/signal:non_fatal_signal_handler_lib",
        "//source/extensions/watchdog/backtrace_action:backtrace_action_lib",
        "//source/extensions/watchdog/backtrace_action:config",
        "//source/server:backtrace_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/synchronization",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/watchdog/backtrace_action/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.watchdog.backtrace_action"],
    rbe_pool = "6gig",
    deps = [
        "//envoy/registry",
        "//envoy/server:guarddog_config_interface",
        "//source/extensions/watchdog/backtrace_action:backtrace_action_lib",
        "//source/extensions/watchdog/backtrace_action:config",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/watchdog/backtrace_action/v3:pkg_cc_proto",
    ],
)
