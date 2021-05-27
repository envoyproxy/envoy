load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "context_interface",
    hdrs = ["context.h"],
)

envoy_cc_library(
    name = "rds_interface",
    hdrs = ["rds.h"],
    deps = [
        ":router_interface",
        "//include/envoy/http:filter_interface_no_include_prefix",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "route_config_provider_manager_interface",
    hdrs = ["route_config_provider_manager.h"],
    deps = [
        ":rds_interface",
        "//include/envoy/event:dispatcher_interface_no_include_prefix",
        "//include/envoy/json:json_object_interface_no_include_prefix",
        "//include/envoy/local_info:local_info_interface_no_include_prefix",
        "//include/envoy/runtime:runtime_interface_no_include_prefix",
        "//include/envoy/server:filter_config_interface_no_include_prefix",
        "//include/envoy/stats:stats_interface_no_include_prefix",
        "//include/envoy/thread_local:thread_local_interface_no_include_prefix",
        "//include/envoy/upstream:cluster_manager_interface_no_include_prefix",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "route_config_update_info_interface",
    hdrs = ["route_config_update_receiver.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":rds_interface",
        "//include/envoy/common:time_interface_no_include_prefix",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "router_interface",
    hdrs = ["router.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":internal_redirect_interface",
        "//include/envoy/access_log:access_log_interface_no_include_prefix",
        "//include/envoy/common:conn_pool_interface_no_include_prefix",
        "//include/envoy/common:matchers_interface_no_include_prefix",
        "//include/envoy/common:time_interface_no_include_prefix",
        "//include/envoy/config:typed_metadata_interface_no_include_prefix",
        "//include/envoy/http:codec_interface_no_include_prefix",
        "//include/envoy/http:codes_interface_no_include_prefix",
        "//include/envoy/http:conn_pool_interface_no_include_prefix",
        "//include/envoy/http:hash_policy_interface_no_include_prefix",
        "//include/envoy/http:header_map_interface_no_include_prefix",
        "//include/envoy/tcp:conn_pool_interface_no_include_prefix",
        "//include/envoy/tracing:http_tracer_interface_no_include_prefix",
        "//include/envoy/upstream:resource_manager_interface_no_include_prefix",
        "//include/envoy/upstream:retry_interface_no_include_prefix",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "scopes_interface",
    hdrs = ["scopes.h"],
    deps = [
        ":router_interface",
        "//include/envoy/config:config_provider_interface_no_include_prefix",
        "//include/envoy/http:header_map_interface_no_include_prefix",
    ],
)

envoy_cc_library(
    name = "router_ratelimit_interface",
    hdrs = ["router_ratelimit.h"],
    deps = [
        "//include/envoy/http:filter_interface_no_include_prefix",
        "//include/envoy/ratelimit:ratelimit_interface_no_include_prefix",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "shadow_writer_interface",
    hdrs = ["shadow_writer.h"],
    deps = [
        "//include/envoy/http:async_client_interface_no_include_prefix",
        "//include/envoy/http:message_interface_no_include_prefix",
    ],
)

envoy_cc_library(
    name = "string_accessor_interface",
    hdrs = ["string_accessor.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/stream_info:filter_state_interface_no_include_prefix",
    ],
)

envoy_cc_library(
    name = "internal_redirect_interface",
    hdrs = ["internal_redirect.h"],
    deps = [
        "//include/envoy/config:typed_config_interface_no_include_prefix",
        "//include/envoy/stream_info:filter_state_interface_no_include_prefix",
        "//source/common/common:minimal_logger_lib",
    ],
)
