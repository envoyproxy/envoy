load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_cc_platform_dep",
    "envoy_cc_posix_library",
    "envoy_cc_win32_library",
    "envoy_package",
    "envoy_select_signal_trace",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "dispatcher_lib",
    srcs = [
        "dispatcher_impl.cc",
        "file_event_impl.cc",
    ],
    hdrs = select({
        "//bazel:windows_x86_64": [
            "win32/signal_impl.h",
        ],
        "//conditions:default": [
            "posix/signal_impl.h",
        ],
    }),
    include_prefix = "source/common/event",
    strip_include_prefix = select({
        "//bazel:windows_x86_64": "win32",
        "//conditions:default": "posix",
    }),
    deps = [
        ":dispatcher_includes",
        ":libevent_scheduler_lib",
        ":real_time_system_lib",
        ":scaled_range_timer_manager_lib",
        ":signal_lib",
        "//envoy/common:scope_tracker_interface",
        "//envoy/common:time_interface",
        "//envoy/event:signal_interface",
        "//envoy/network:client_connection_factory",
        "//source/common/common:assert_lib",
        "//source/common/common:thread_lib",
        "//source/common/config:utility_lib",
        "//source/common/filesystem:watcher_lib",
        "//source/common/network:address_lib",
        "//source/common/network:default_client_connection_factory",
        "@envoy_api//envoy/config/overload/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "signal_lib",
    deps = envoy_cc_platform_dep("signal_impl_lib"),
)

envoy_cc_posix_library(
    name = "signal_impl_lib",
    srcs = ["posix/signal_impl.cc"],
    hdrs = ["posix/signal_impl.h"],
    include_prefix = "source/common/event",
    strip_include_prefix = "posix",
    deps = [
        ":dispatcher_includes",
        "//envoy/event:signal_interface",
        "//source/common/common:thread_lib",
    ],
)

envoy_cc_win32_library(
    name = "signal_impl_lib",
    srcs = ["win32/signal_impl.cc"],
    hdrs = ["win32/signal_impl.h"],
    include_prefix = "source/common/event",
    strip_include_prefix = "win32",
    deps = [
        ":dispatcher_includes",
        "//envoy/event:signal_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:thread_lib",
        "//source/common/network:default_socket_interface_lib",
    ],
)

envoy_cc_library(
    name = "event_impl_base_lib",
    srcs = ["event_impl_base.cc"],
    hdrs = ["event_impl_base.h"],
    deps = [
        "//bazel/foreign_cc:event",
    ],
)

envoy_cc_library(
    name = "real_time_system_lib",
    srcs = ["real_time_system.cc"],
    hdrs = ["real_time_system.h"],
    deps = [
        ":event_impl_base_lib",
        ":timer_lib",
        "//envoy/event:timer_interface",
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
        "//envoy/api:api_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:file_event_interface",
        "//envoy/network:connection_handler_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/signal:fatal_error_handler_lib",
        "@com_google_absl//absl/container:inlined_vector",
    ] + envoy_select_signal_trace(["//source/common/signal:sigaction_lib"]),
)

envoy_cc_library(
    name = "libevent_lib",
    srcs = ["libevent.cc"],
    hdrs = ["libevent.h"],
    deps = [
        "//bazel/foreign_cc:event",
        "//source/common/common:assert_lib",
        "//source/common/common:c_smart_ptr_lib",
    ],
)

envoy_cc_library(
    name = "libevent_scheduler_lib",
    srcs = ["libevent_scheduler.cc"],
    hdrs = ["libevent_scheduler.h"],
    deps = [
        ":libevent_lib",
        ":schedulable_cb_lib",
        ":timer_lib",
        "//bazel/foreign_cc:event",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "schedulable_cb_lib",
    srcs = ["schedulable_cb_impl.cc"],
    hdrs = ["schedulable_cb_impl.h"],
    deps = [
        ":event_impl_base_lib",
        ":libevent_lib",
        "//bazel/foreign_cc:event",
        "//envoy/event:schedulable_cb_interface",
    ],
)

envoy_cc_library(
    name = "timer_lib",
    srcs = ["timer_impl.cc"],
    hdrs = ["timer_impl.h"],
    deps = [
        ":event_impl_base_lib",
        ":libevent_lib",
        "//bazel/foreign_cc:event",
        "//envoy/event:timer_interface",
        "//source/common/common:scope_tracker",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "deferred_task",
    hdrs = ["deferred_task.h"],
    deps = [
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
    ],
)

envoy_cc_library(
    name = "scaled_range_timer_manager_lib",
    srcs = ["scaled_range_timer_manager_impl.cc"],
    hdrs = ["scaled_range_timer_manager_impl.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/event:scaled_range_timer_manager_interface",
        "//envoy/event:timer_interface",
        "//source/common/common:scope_tracker",
    ],
)
