licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "config_lib",
    srcs = ["config_impl.cc"],
    hdrs = ["config_impl.h"],
    deps = [
        ":config_utility_lib",
        ":retry_state_lib",
        ":router_ratelimit_lib",
        "//include/envoy/common:optional",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/router:router_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:utility_lib",
        "//source/common/config:rds_json_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "config_utility_lib",
    srcs = ["config_utility.cc"],
    hdrs = ["config_utility.h"],
    external_deps = ["envoy_rds"],
    deps = [
        "//include/envoy/json:json_object_interface",
        "//include/envoy/upstream:resource_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/config:rds_json_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "rds_lib",
    srcs = ["rds_impl.cc"],
    hdrs = ["rds_impl.h"],
    deps = [
        ":config_lib",
        "//include/envoy/init:init_interface",
        "//include/envoy/json:json_object_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/router:rds_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:rest_api_fetcher_lib",
        "//source/common/json:config_schemas_lib",
    ],
)

envoy_cc_library(
    name = "retry_state_lib",
    srcs = ["retry_state_impl.cc"],
    hdrs = ["retry_state_impl.h"],
    deps = [
        "//include/envoy/common:optional",
        "//include/envoy/event:timer_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/router:router_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/grpc:common_lib",
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
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:conn_pool_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/router:shadow_writer_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:upstream_interface",
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
        "//include/envoy/router:router_interface",
        "//include/envoy/router:router_ratelimit_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/config:rds_json_lib",
        "//source/common/http/filter:ratelimit_includes",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "shadow_writer_lib",
    srcs = ["shadow_writer_impl.cc"],
    hdrs = ["shadow_writer_impl.h"],
    deps = [
        "//include/envoy/router:shadow_writer_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/http:headers_lib",
    ],
)
