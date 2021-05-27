load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "event_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/event:deferred_deletable_no_include_prefix",
        "//include/envoy/event:dispatcher_interface_no_include_prefix",
        "//include/envoy/event:file_event_interface_no_include_prefix",
        "//include/envoy/event:scaled_range_timer_manager_interface_no_include_prefix",
        "//include/envoy/event:signal_interface_no_include_prefix",
        "//include/envoy/event:timer_interface_no_include_prefix",
        "//include/envoy/network:connection_handler_interface_no_include_prefix",
        "//include/envoy/network:connection_interface_no_include_prefix",
        "//include/envoy/network:dns_interface_no_include_prefix",
        "//include/envoy/network:listener_interface_no_include_prefix",
        "//include/envoy/ssl:context_interface_no_include_prefix",
        "//test/mocks/buffer:buffer_mocks",
        "//test/test_common:test_time_lib",
    ],
)

envoy_cc_test_library(
    name = "wrapped_dispatcher",
    hdrs = ["wrapped_dispatcher.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface_no_include_prefix",
    ],
)
