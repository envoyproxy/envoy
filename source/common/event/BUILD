licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "dispatcher_lib",
    srcs = [
        "dispatcher_impl.cc",
        "event_impl_base.cc",
        "file_event_impl.cc",
        "signal_impl.cc",
        "timer_impl.cc",
    ],
    hdrs = [
        "signal_impl.h",
        "timer_impl.h",
    ],
    deps = [
        ":dispatcher_includes",
        "//include/envoy/event:signal_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/network:listen_socket_interface",
        "//include/envoy/network:listener_interface",
        "//source/common/common:assert_lib",
        "//source/common/filesystem:watcher_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:dns_lib",
        "//source/common/network:listener_lib",
    ],
)

envoy_cc_library(
    name = "dispatcher_includes",
    hdrs = [
        "dispatcher_impl.h",
        "event_impl_base.h",
        "file_event_impl.h",
    ],
    deps = [
        ":libevent_lib",
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:file_event_interface",
        "//include/envoy/network:connection_handler_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_lib",
    ],
)

envoy_cc_library(
    name = "libevent_lib",
    srcs = ["libevent.cc"],
    hdrs = ["libevent.h"],
    libevent_dep = 1,
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:c_smart_ptr_lib",
    ],
)

envoy_cc_library(
    name = "dispatched_thread_lib",
    srcs = ["dispatched_thread.cc"],
    hdrs = ["dispatched_thread.h"],
    libevent_dep = 1,
    deps = [
        ":dispatcher_lib",
        "//include/envoy/event:dispatcher_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_lib",
        "//source/server:guarddog_lib",
    ],
)
