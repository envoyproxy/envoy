package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "deferred_deletable",
    hdrs = ["deferred_deletable.h"],
)

envoy_cc_library(
    name = "dispatcher_includes",
    hdrs = ["dispatcher.h"],
    deps = [
        ":deferred_deletable",
        ":file_event_includes",
        ":signal_includes",
        ":timer_includes",
        "//include/envoy/filesystem:filesystem_includes",
        "//include/envoy/network:connection_handler_includes",
        "//include/envoy/network:connection_includes",
        "//include/envoy/network:dns_includes",
        "//include/envoy/network:listen_socket_includes",
        "//include/envoy/network:listener_includes",
    ],
)

envoy_cc_library(
    name = "file_event_includes",
    hdrs = ["file_event.h"],
)

envoy_cc_library(
    name = "signal_includes",
    hdrs = ["signal.h"],
)

envoy_cc_library(
    name = "timer_includes",
    hdrs = ["timer.h"],
)
