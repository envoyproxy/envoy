load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "dispatcher_lib",
    srcs = [
        "dispatcher_impl.cc",
        "file_event_impl.cc",
        "signal_impl.cc",
    ],
    hdrs = [
        "signal_impl.h",
    ],
    deps = [
        ":dispatcher_includes",
        ":libevent_scheduler_lib",
        ":real_time_system_lib",
        "//include/envoy/common:scope_tracker_interface",
        "//include/envoy/common:time_interface",
        "//include/envoy/event:signal_interface",
        "//include/envoy/network:listen_socket_interface",
        "//include/envoy/network:listener_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:thread_lib",
        "//source/common/filesystem:watcher_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:dns_lib",
        "//source/common/network:listener_lib",
        "//source/common/runtime:runtime_features_lib",
    ],
)

envoy_cc_library(
    name = "event_impl_base_lib",
    srcs = ["event_impl_base.cc"],
    hdrs = ["event_impl_base.h"],
    external_deps = [
        "event",
    ],
)

envoy_cc_library(
    name = "real_time_system_lib",
    srcs = ["real_time_system.cc"],
    hdrs = ["real_time_system.h"],
    deps = [
        ":event_impl_base_lib",
        ":timer_lib",
        "//include/envoy/event:timer_interface",
        "//source/common/common:utility_lib",
        "//source/common/event:dispatcher_includes",
    ],
)

envoy_cc_library(
    name = "dispatcher_includes",
    hdrs = [
        "dispatcher_impl.h",
        "event_impl_base.h",
        "file_event_impl.h",
        "schedulable_cb_impl.h",
    ],
    deps = [
        ":libevent_lib",
        ":libevent_scheduler_lib",
        "//include/envoy/api:api_interface",
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:file_event_interface",
        "//include/envoy/network:connection_handler_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/signal:fatal_error_handler_lib",
    ] + select({
        "//bazel:disable_signal_trace": [],
        "//conditions:default": [
            "//source/common/signal:sigaction_lib",
        ],
    }),
)

envoy_cc_library(
    name = "libevent_lib",
    srcs = ["libevent.cc"],
    hdrs = ["libevent.h"],
    external_deps = [
        "event",
    ],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:c_smart_ptr_lib",
    ],
)

envoy_cc_library(
    name = "libevent_scheduler_lib",
    srcs = ["libevent_scheduler.cc"],
    hdrs = ["libevent_scheduler.h"],
    external_deps = ["event"],
    deps = [
        ":libevent_lib",
        ":schedulable_cb_lib",
        ":timer_lib",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "schedulable_cb_lib",
    srcs = ["schedulable_cb_impl.cc"],
    hdrs = ["schedulable_cb_impl.h"],
    external_deps = ["event"],
    deps = [
        ":event_impl_base_lib",
        ":libevent_lib",
        "//include/envoy/event:schedulable_cb_interface",
    ],
)

envoy_cc_library(
    name = "timer_lib",
    srcs = ["timer_impl.cc"],
    hdrs = ["timer_impl.h"],
    external_deps = ["event"],
    deps = [
        ":event_impl_base_lib",
        ":libevent_lib",
        "//include/envoy/event:timer_interface",
        "//source/common/common:scope_tracker",
    ],
)

envoy_cc_library(
    name = "deferred_task",
    hdrs = ["deferred_task.h"],
    deps = [
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/event:dispatcher_interface",
    ],
)
