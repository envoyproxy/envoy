licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "admin_interface",
    hdrs = ["admin.h"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/http:codes_interface",
    ],
)

envoy_cc_library(
    name = "configuration_interface",
    hdrs = ["configuration.h"],
    deps = [
        "//include/envoy/common:optional",
        "//include/envoy/ratelimit:ratelimit_interface",
        "//include/envoy/ssl:context_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//include/envoy/upstream:cluster_manager_interface",
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
    name = "hot_restart_interface",
    hdrs = ["hot_restart.h"],
    deps = ["//include/envoy/event:dispatcher_interface"],
)

envoy_cc_library(
    name = "instance_interface",
    hdrs = ["instance.h"],
    deps = [
        ":admin_interface",
        ":drain_manager_interface",
        ":hot_restart_interface",
        ":options_interface",
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/api:api_interface",
        "//include/envoy/init:init_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/ratelimit:ratelimit_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/ssl:context_manager_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//include/envoy/upstream:cluster_manager_interface",
    ],
)

envoy_cc_library(
    name = "options_interface",
    hdrs = ["options.h"],
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
        ":drain_manager_interface",
        ":instance_interface",
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/init:init_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/ratelimit:ratelimit_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//include/envoy/upstream:cluster_manager_interface",
    ],
)
