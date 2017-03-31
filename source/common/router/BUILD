package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "config_lib",
    srcs = ["config_impl.cc"],
    hdrs = ["config_impl.h"],
    deps = [
        ":config_utility_lib",
        ":retry_state_lib",
        ":router_ratelimit_lib",
        "//include/envoy/common:optional_includes",
        "//include/envoy/http:header_map_includes",
        "//include/envoy/router:router_includes",
        "//include/envoy/runtime:runtime_includes",
        "//include/envoy/upstream:cluster_manager_includes",
        "//include/envoy/upstream:upstream_includes",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/json:json_loader_lib",
    ],
)

envoy_cc_library(
    name = "config_utility_lib",
    srcs = ["config_utility.cc"],
    hdrs = ["config_utility.h"],
    deps = [
        "//include/envoy/json:json_object_includes",
        "//include/envoy/upstream:resource_manager_includes",
        "//source/common/common:empty_string",
        "//source/common/http:headers_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/json:json_validator_lib",
    ],
)

envoy_cc_library(
    name = "rds_lib",
    srcs = ["rds_impl.cc"],
    hdrs = ["rds_impl.h"],
    deps = [
        ":config_lib",
        "//include/envoy/init:init_includes",
        "//include/envoy/json:json_object_includes",
        "//include/envoy/local_info:local_info_includes",
        "//include/envoy/router:rds_includes",
        "//include/envoy/thread_local:thread_local_includes",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/http:rest_api_fetcher_lib",
        "//source/common/json:config_schemas_lib",
    ],
)

envoy_cc_library(
    name = "retry_state_lib",
    srcs = ["retry_state_impl.cc"],
    hdrs = ["retry_state_impl.h"],
    deps = [
        "//include/envoy/common:optional_includes",
        "//include/envoy/event:timer_includes",
        "//include/envoy/http:codec_includes",
        "//include/envoy/http:header_map_includes",
        "//include/envoy/router:router_includes",
        "//include/envoy/runtime:runtime_includes",
        "//include/envoy/upstream:upstream_includes",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
    ],
)

envoy_cc_library(
    name = "router_lib",
    srcs = ["router.cc"],
    hdrs = ["router.h"],
    deps = [
        ":config_lib",
        ":retry_state_lib",
        "//include/envoy/event:dispatcher_includes",
        "//include/envoy/event:timer_includes",
        "//include/envoy/http:codec_includes",
        "//include/envoy/http:codes_includes",
        "//include/envoy/http:conn_pool_includes",
        "//include/envoy/http:filter_includes",
        "//include/envoy/local_info:local_info_includes",
        "//include/envoy/router:shadow_writer_includes",
        "//include/envoy/runtime:runtime_includes",
        "//include/envoy/stats:stats_includes",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/upstream:cluster_manager_includes",
        "//include/envoy/upstream:upstream_includes",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
    ],
)

envoy_cc_library(
    name = "router_ratelimit_lib",
    srcs = ["router_ratelimit.cc"],
    hdrs = ["router_ratelimit.h"],
    deps = [
        ":config_utility_lib",
        "//include/envoy/router:router_includes",
        "//include/envoy/router:router_ratelimit_includes",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/http/filter:ratelimit_lib_includes",
        "//source/common/json:config_schemas_lib",
        "//source/common/json:json_validator_lib",
    ],
)

envoy_cc_library(
    name = "shadow_writer_lib",
    srcs = ["shadow_writer_impl.cc"],
    hdrs = ["shadow_writer_impl.h"],
    deps = [
        "//include/envoy/router:shadow_writer_includes",
        "//include/envoy/upstream:cluster_manager_includes",
        "//source/common/common:assert_lib",
        "//source/common/http:headers_lib",
    ],
)
