licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "rds_interface",
    hdrs = ["rds.h"],
    deps = [":router_interface"],
)

envoy_cc_library(
    name = "route_config_provider_manager_interface",
    hdrs = ["route_config_provider_manager.h"],
    deps = [
        ":rds_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/init:init_interface",
        "//include/envoy/json:json_object_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "@envoy_api//envoy/config/filter/network/http_connection_manager/v2:http_connection_manager_cc",
    ],
)

envoy_cc_library(
    name = "router_interface",
    hdrs = ["router.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//include/envoy/upstream:resource_manager_interface",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "router_ratelimit_interface",
    hdrs = ["router_ratelimit.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/ratelimit:ratelimit_interface",
    ],
)

envoy_cc_library(
    name = "shadow_writer_interface",
    hdrs = ["shadow_writer.h"],
    deps = ["//include/envoy/http:message_interface"],
)
