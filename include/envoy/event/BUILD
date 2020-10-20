load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "deferred_deletable",
    hdrs = ["deferred_deletable.h"],
)

envoy_cc_library(
    name = "dispatcher_interface",
    hdrs = ["dispatcher.h"],
    deps = [
        ":deferred_deletable",
        ":file_event_interface",
        ":schedulable_cb_interface",
        ":signal_interface",
        "//include/envoy/common:scope_tracker_interface",
        "//include/envoy/common:time_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/filesystem:watcher_interface",
        "//include/envoy/network:connection_handler_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:dns_interface",
        "//include/envoy/network:listen_socket_interface",
        "//include/envoy/network:listener_interface",
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/thread:thread_interface",
    ],
)

envoy_cc_library(
    name = "file_event_interface",
    hdrs = ["file_event.h"],
)

envoy_cc_library(
    name = "scaled_range_timer_manager_interface",
    hdrs = ["scaled_range_timer_manager.h"],
    deps = [
        ":timer_interface",
    ],
)

envoy_cc_library(
    name = "schedulable_cb_interface",
    hdrs = ["schedulable_cb.h"],
)

envoy_cc_library(
    name = "signal_interface",
    hdrs = ["signal.h"],
)

envoy_cc_library(
    name = "timer_interface",
    hdrs = ["timer.h"],
    deps = [
        ":schedulable_cb_interface",
        "//include/envoy/common:time_interface",
    ],
)
