package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_mock")

envoy_cc_mock(
    name = "event_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/event:dispatcher_includes",
        "//include/envoy/event:file_event_includes",
        "//include/envoy/event:signal_includes",
        "//include/envoy/event:timer_includes",
        "//include/envoy/network:connection_handler_includes",
        "//include/envoy/network:connection_includes",
        "//include/envoy/network:dns_includes",
        "//include/envoy/network:listener_includes",
        "//include/envoy/ssl:context_includes",
        "//source/common/network:listen_socket_lib",
        "//source/common/stats:stats_lib",
    ],
)
