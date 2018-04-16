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
    external_deps = ["abseil_optional"],
    deps = [
        ":config_utility_lib",
        ":header_formatter_lib",
        ":header_parser_lib",
        ":retry_state_lib",
        ":router_ratelimit_lib",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/router:router_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/server:filter_config_interface",  # TODO(rodaine): break dependency on server
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:hash_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:metadata_lib",
        "//source/common/config:rds_json_lib",
        "//source/common/config:utility_lib",
        "//source/common/config:well_known_names",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "config_utility_lib",
    srcs = ["config_utility.cc"],
    hdrs = ["config_utility.h"],
    deps = [
        "//include/envoy/http:codes_interface",
        "//include/envoy/upstream:resource_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/config:rds_json_lib",
        "//source/common/filesystem:filesystem_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/api/v2:rds_cc",
    ],
)

envoy_cc_library(
    name = "rds_lib",
    srcs = ["rds_impl.cc"],
    hdrs = ["rds_impl.h"],
    deps = [
        ":config_lib",
        ":rds_subscription_lib",
        "//include/envoy/config:subscription_interface",
        "//include/envoy/http:codes_interface",
        "//include/envoy/init:init_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/router:rds_interface",
        "//include/envoy/router:route_config_provider_manager_interface",
        "//include/envoy/server:admin_interface",
        "//include/envoy/singleton:instance_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/config:rds_json_lib",
        "//source/common/config:subscription_factory_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/admin/v2:config_dump_cc",
        "@envoy_api//envoy/api/v2:rds_cc",
        "@envoy_api//envoy/config/filter/network/http_connection_manager/v2:http_connection_manager_cc",
    ],
)

envoy_cc_library(
    name = "rds_subscription_lib",
    srcs = ["rds_subscription.cc"],
    hdrs = ["rds_subscription.h"],
    deps = [
        "//include/envoy/config:subscription_interface",
        "//source/common/common:assert_lib",
        "//source/common/config:rds_json_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:rest_api_fetcher_lib",
        "//source/common/json:json_loader_lib",
        "@envoy_api//envoy/config/filter/network/http_connection_manager/v2:http_connection_manager_cc",
    ],
)

envoy_cc_library(
    name = "retry_state_lib",
    srcs = ["retry_state_impl.cc"],
    hdrs = ["retry_state_impl.h"],
    external_deps = ["abseil_optional"],
    deps = [
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
        ":header_parser_lib",
        ":retry_state_lib",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/grpc:status",
        "//include/envoy/http:codec_interface",
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:conn_pool_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/router:shadow_writer_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:hash_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/filesystem:filesystem_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/request_info:request_info_lib",
        "//source/common/tracing:http_tracer_lib",
        "@envoy_api//envoy/config/filter/http/router/v2:router_cc",
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

envoy_cc_library(
    name = "header_parser_lib",
    srcs = ["header_parser.cc"],
    hdrs = ["header_parser.h"],
    deps = [
        ":header_formatter_lib",
        "//include/envoy/http:header_map_interface",
        "//source/common/config:base_json_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "header_formatter_lib",
    srcs = ["header_formatter.cc"],
    hdrs = ["header_formatter.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//source/common/access_log:access_log_formatter_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:metadata_lib",
        "//source/common/json:json_loader_lib",
    ],
)
