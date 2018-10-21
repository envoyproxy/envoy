licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "access_log_config_interface",
    hdrs = ["access_log_config.h"],
    deps = [
        ":filter_config_interface",
        "//include/envoy/access_log:access_log_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "admin_interface",
    hdrs = ["admin.h"],
    deps = [
        ":config_tracker_interface",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/http:query_params_interface",
        "//include/envoy/network:listen_socket_interface",
    ],
)

envoy_cc_library(
    name = "configuration_interface",
    hdrs = ["configuration.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/ratelimit:ratelimit_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//include/envoy/upstream:cluster_manager_interface",
    ],
)

envoy_cc_library(
    name = "config_tracker_interface",
    hdrs = ["config_tracker.h"],
    deps = [
        "//source/common/common:non_copyable",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "drain_manager_interface",
    hdrs = ["drain_manager.h"],
    deps = ["//include/envoy/network:drain_decision_interface"],
)

envoy_cc_library(
    name = "guarddog_interface",
    hdrs = ["guarddog.h"],
    deps = [
        "//include/envoy/server:watchdog_interface",
        "//include/envoy/stats:stats_interface",
    ],
)

envoy_cc_library(
    name = "health_checker_config_interface",
    hdrs = ["health_checker_config.h"],
    deps = ["//include/envoy/upstream:health_checker_interface"],
)

envoy_cc_library(
    name = "hot_restart_interface",
    hdrs = ["hot_restart.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/thread:thread_interface",
    ],
)

envoy_cc_library(
    name = "instance_interface",
    hdrs = ["instance.h"],
    deps = [
        ":admin_interface",
        ":drain_manager_interface",
        ":hot_restart_interface",
        ":listener_manager_interface",
        ":options_interface",
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/api:api_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/http:query_params_interface",
        "//include/envoy/init:init_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/ratelimit:ratelimit_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/secret:secret_manager_interface",
        "//include/envoy/server:overload_manager_interface",
        "//include/envoy/ssl:context_manager_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//include/envoy/upstream:cluster_manager_interface",
    ],
)

envoy_cc_library(
    name = "options_interface",
    hdrs = ["options.h"],
    deps = [
        "//include/envoy/network:address_interface",
        "//include/envoy/stats:stats_interface",
    ],
)

envoy_cc_library(
    name = "worker_interface",
    hdrs = ["worker.h"],
    deps = [
        "//include/envoy/server:guarddog_interface",
    ],
)

envoy_cc_library(
    name = "watchdog_interface",
    hdrs = ["watchdog.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:address_interface",
    ],
)

envoy_cc_library(
    name = "filter_config_interface",
    hdrs = ["filter_config.h"],
    deps = [
        ":admin_interface",
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/init:init_interface",
        "//include/envoy/json:json_object_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/network:drain_decision_interface",
        "//include/envoy/ratelimit:ratelimit_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/server:overload_manager_interface",
        "//include/envoy/singleton:manager_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:macros",
        "//source/common/protobuf",
        "@envoy_api//envoy/api/v2/core:base_cc",
    ],
)

envoy_cc_library(
    name = "listener_manager_interface",
    hdrs = ["listener_manager.h"],
    deps = [
        ":drain_manager_interface",
        ":filter_config_interface",
        ":guarddog_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/network:listen_socket_interface",
        "//include/envoy/ssl:context_interface",
        "//source/common/protobuf",
        "@envoy_api//envoy/api/v2/listener:listener_cc",
    ],
)

envoy_cc_library(
    name = "transport_socket_config_interface",
    hdrs = ["transport_socket_config.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/init:init_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/secret:secret_manager_interface",
        "//include/envoy/ssl:context_manager_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "resource_monitor_interface",
    hdrs = ["resource_monitor.h"],
    deps = [
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "resource_monitor_config_interface",
    hdrs = ["resource_monitor_config.h"],
    deps = [
        ":resource_monitor_interface",
        "//include/envoy/event:dispatcher_interface",
    ],
)

envoy_cc_library(
    name = "overload_manager_interface",
    hdrs = ["overload_manager.h"],
    deps = [
    ],
)
