package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "admin_includes",
    hdrs = ["admin.h"],
    deps = [
        "//include/envoy/buffer:buffer_includes",
        "//include/envoy/http:codes_includes",
    ],
)

envoy_cc_library(
    name = "configuration_includes",
    hdrs = ["configuration.h"],
    deps = [
        "//include/envoy/common:optional_includes",
        "//include/envoy/ratelimit:ratelimit_includes",
        "//include/envoy/ssl:context_includes",
        "//include/envoy/tracing:http_tracer_includes",
        "//include/envoy/upstream:cluster_manager_includes",
    ],
)

envoy_cc_library(
    name = "drain_manager_includes",
    hdrs = ["drain_manager.h"],
    deps = ["//include/envoy/network:drain_decision_includes"],
)

envoy_cc_library(
    name = "guarddog_includes",
    hdrs = ["guarddog.h"],
    deps = [
        "//include/envoy/server:watchdog_includes",
        "//include/envoy/stats:stats_includes",
    ],
)

envoy_cc_library(
    name = "hot_restart_includes",
    hdrs = ["hot_restart.h"],
    deps = ["//include/envoy/event:dispatcher_includes"],
)

envoy_cc_library(
    name = "instance_includes",
    hdrs = ["instance.h"],
    deps = [
        ":admin_includes",
        ":drain_manager_includes",
        ":hot_restart_includes",
        ":options_includes",
        "//include/envoy/access_log:access_log_includes",
        "//include/envoy/api:api_includes",
        "//include/envoy/init:init_includes",
        "//include/envoy/local_info:local_info_includes",
        "//include/envoy/ratelimit:ratelimit_includes",
        "//include/envoy/runtime:runtime_includes",
        "//include/envoy/ssl:context_manager_includes",
        "//include/envoy/thread_local:thread_local_includes",
        "//include/envoy/tracing:http_tracer_includes",
        "//include/envoy/upstream:cluster_manager_includes",
    ],
)

envoy_cc_library(
    name = "options_includes",
    hdrs = ["options.h"],
)

envoy_cc_library(
    name = "watchdog_includes",
    hdrs = ["watchdog.h"],
    deps = ["//include/envoy/event:dispatcher_includes"],
)
