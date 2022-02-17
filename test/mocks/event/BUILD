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
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:file_event_interface",
        "//envoy/event:scaled_range_timer_manager_interface",
        "//envoy/event:signal_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:connection_handler_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:dns_interface",
        "//envoy/network:listener_interface",
        "//envoy/ssl:context_interface",
        "//test/mocks/buffer:buffer_mocks",
        "//test/test_common:test_time_lib",
    ],
)

envoy_cc_test_library(
    name = "wrapped_dispatcher",
    hdrs = ["wrapped_dispatcher.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
    ],
)
