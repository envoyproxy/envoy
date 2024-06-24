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
    name = "dispatcher_thread_deletable",
    hdrs = ["dispatcher_thread_deletable.h"],
)

envoy_cc_library(
    name = "dispatcher_interface",
    hdrs = ["dispatcher.h"],
    deps = [
        ":deferred_deletable",
        ":dispatcher_thread_deletable",
        ":file_event_interface",
        ":scaled_timer",
        ":schedulable_cb_interface",
        ":signal_interface",
        ":timer_interface",
        "//envoy/common:scope_tracker_interface",
        "//envoy/common:time_interface",
        "//envoy/filesystem:watcher_interface",
        "//envoy/network:connection_handler_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:dns_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/network:listener_interface",
        "//envoy/network:transport_socket_interface",
        "//envoy/server:watchdog_interface",
        "//envoy/server/overload:thread_local_overload_state",
        "//envoy/thread:thread_interface",
        "@com_google_absl//absl/functional:any_invocable",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "file_event_interface",
    hdrs = ["file_event.h"],
)

envoy_cc_library(
    name = "scaled_timer",
    hdrs = ["scaled_timer.h"],
    deps = [
        "//source/common/common:interval_value",
    ],
)

envoy_cc_library(
    name = "scaled_range_timer_manager_interface",
    hdrs = ["scaled_range_timer_manager.h"],
    deps = [
        ":scaled_timer",
        ":timer_interface",
    ],
)

envoy_cc_library(
    name = "schedulable_cb_interface",
    hdrs = ["schedulable_cb.h"],
    deps = ["//envoy/common:pure_lib"],
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
        "//envoy/common:time_interface",
    ],
)
