load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "admin_lib",
    srcs = ["admin.cc"],
    hdrs = ["admin.h"],
    deps = [
        ":admin_filter_lib",
        ":config_tracker_lib",
        ":listeners_handler_lib",
        ":logs_handler_lib",
        ":profiling_handler_lib",
        ":runtime_handler_lib",
        ":server_cmd_handler_lib",
        ":server_info_handler_lib",
        ":stats_handler_lib",
        ":utils_lib",
        "//include/envoy/filesystem:filesystem_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/http:request_id_extension_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/network:listen_socket_interface",
        "//include/envoy/server:admin_interface",
        "//include/envoy/server:hot_restart_interface",
        "//include/envoy/server:instance_interface",
        "//include/envoy/server:listener_manager_interface",
        "//include/envoy/server:options_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:resource_manager_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:basic_resource_lib",
        "//source/common/common:empty_string",
        "//source/common/common:macros",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:mutex_tracer_lib",
        "//source/common/common:utility_lib",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/html:utility_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:conn_manager_lib",
        "//source/common/http:date_provider_lib",
        "//source/common/http:default_server_string_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:request_id_extension_lib",
        "//source/common/http:utility_lib",
        "//source/common/memory:utils_lib",
        "//source/common/network:connection_balancer_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:raw_buffer_socket_lib",
        "//source/common/network:utility_lib",
        "//source/common/router:config_lib",
        "//source/common/router:scoped_config_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/upstream:host_utility_lib",
        "//source/extensions/access_loggers/file:file_access_log_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "admin_filter_lib",
    srcs = ["admin_filter.cc"],
    hdrs = ["admin_filter.h"],
    deps = [
        ":utils_lib",
        "//include/envoy/http:filter_interface",
        "//include/envoy/server:admin_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_library(
    name = "handler_ctx_lib",
    hdrs = ["handler_ctx.h"],
    deps = [
        "//include/envoy/server:instance_interface",
    ],
)

envoy_cc_library(
    name = "stats_handler_lib",
    srcs = ["stats_handler.cc"],
    hdrs = ["stats_handler.h"],
    deps = [
        ":handler_ctx_lib",
        ":prometheus_stats_lib",
        ":utils_lib",
        "//include/envoy/http:codes_interface",
        "//include/envoy/server:admin_interface",
        "//include/envoy/server:instance_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/html:utility_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/stats:histogram_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "prometheus_stats_lib",
    srcs = ["prometheus_stats.cc"],
    hdrs = ["prometheus_stats.h"],
    deps = [
        ":utils_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/stats:histogram_lib",
    ],
)

envoy_cc_library(
    name = "listeners_handler_lib",
    srcs = ["listeners_handler.cc"],
    hdrs = ["listeners_handler.h"],
    deps = [
        ":handler_ctx_lib",
        ":utils_lib",
        "//include/envoy/http:codes_interface",
        "//include/envoy/server:admin_interface",
        "//include/envoy/server:instance_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "runtime_handler_lib",
    srcs = ["runtime_handler.cc"],
    hdrs = ["runtime_handler.h"],
    deps = [
        ":handler_ctx_lib",
        ":utils_lib",
        "//include/envoy/http:codes_interface",
        "//include/envoy/server:admin_interface",
        "//include/envoy/server:instance_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
    ],
)

envoy_cc_library(
    name = "logs_handler_lib",
    srcs = ["logs_handler.cc"],
    hdrs = ["logs_handler.h"],
    deps = [
        ":handler_ctx_lib",
        ":utils_lib",
        "//include/envoy/http:codes_interface",
        "//include/envoy/server:admin_interface",
        "//include/envoy/server:instance_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
    ],
)

envoy_cc_library(
    name = "profiling_handler_lib",
    srcs = ["profiling_handler.cc"],
    hdrs = ["profiling_handler.h"],
    deps = [
        ":utils_lib",
        "//include/envoy/http:codes_interface",
        "//include/envoy/server:admin_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/profiler:profiler_lib",
    ],
)

envoy_cc_library(
    name = "server_cmd_handler_lib",
    srcs = ["server_cmd_handler.cc"],
    hdrs = ["server_cmd_handler.h"],
    deps = [
        ":handler_ctx_lib",
        ":utils_lib",
        "//include/envoy/http:codes_interface",
        "//include/envoy/server:admin_interface",
        "//include/envoy/server:instance_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
    ],
)

envoy_cc_library(
    name = "server_info_handler_lib",
    srcs = ["server_info_handler.cc"],
    hdrs = ["server_info_handler.h"],
    deps = [
        ":handler_ctx_lib",
        ":utils_lib",
        "//include/envoy/http:codes_interface",
        "//include/envoy/server:admin_interface",
        "//include/envoy/server:instance_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/memory:stats_lib",
        "//source/common/version:version_includes",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "utils_lib",
    srcs = ["utils.cc"],
    hdrs = ["utils.h"],
    deps = [
        "//include/envoy/init:manager_interface",
        "//source/common/common:enum_to_int",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "config_tracker_lib",
    srcs = ["config_tracker_impl.cc"],
    hdrs = ["config_tracker_impl.h"],
    deps = [
        "//include/envoy/server:config_tracker_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:macros",
    ],
)
