load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "backtrace_action_lib",
    srcs = ["backtrace_action.cc"],
    hdrs = ["backtrace_action.h"],
    deps = [
        "//envoy/common:time_interface",
        "//envoy/event:timer_interface",
        "//envoy/server:guarddog_config_interface",
        "//envoy/stats:stats_macros",
        "//envoy/thread:thread_interface",
        "//source/common/common:thread_impl_lib_posix",
        "//source/common/protobuf:utility_lib",
        "//source/common/signal:non_fatal_signal_handler_lib",
        "//source/common/thread:signal_thread_lib",
        "//source/server:backtrace_lib",
        "@abseil-cpp//absl/debugging:stacktrace",
        "@envoy_api//envoy/extensions/watchdog/backtrace_action/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":backtrace_action_lib",
        "//envoy/registry",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:message_validator_lib",
        "@envoy_api//envoy/extensions/watchdog/backtrace_action/v3:pkg_cc_proto",
    ],
)
