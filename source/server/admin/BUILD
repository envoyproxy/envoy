load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_admin_functionality",
    "envoy_select_admin_html",
    "envoy_select_admin_no_html",
)

licenses(["notice"])  # Apache 2

# Admin libraries are private (tests exempted) by default.
# Any admin libraries with public visibility must have envoy_select_admin_functionality rules.
envoy_package(["//test:__subpackages__"])

envoy_cc_library(
    name = "admin_lib",
    visibility = ["//visibility:public"],
    deps = [] + envoy_select_admin_functionality(["//source/server/admin:admin_lib_internal"]),
)

envoy_cc_library(
    name = "admin_lib_internal",
    srcs = ["admin.cc"] + envoy_select_admin_html([
        "admin_html.cc",
    ]) + envoy_select_admin_no_html([
        "admin_no_html.cc",
    ]),
    hdrs = ["admin.h"],
    deps = [
        ":admin_filter_lib",
        ":clusters_handler_lib",
        ":config_dump_handler_lib",
        ":config_tracker_lib",
        ":init_dump_handler_lib",
        ":listeners_handler_lib",
        ":logs_handler_lib",
        ":profiling_handler_lib",
        ":runtime_handler_lib",
        ":server_cmd_handler_lib",
        ":server_info_handler_lib",
        ":stats_handler_lib",
        ":utils_lib",
        "//envoy/filesystem:filesystem_interface",
        "//envoy/http:filter_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/server:admin_interface",
        "//envoy/server:hot_restart_interface",
        "//envoy/server:instance_interface",
        "//envoy/server:listener_manager_interface",
        "//envoy/server:options_interface",
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
        "//source/common/http:codes_lib",
        "//source/common/http:conn_manager_lib",
        "//source/common/http:date_provider_lib",
        "//source/common/http:default_server_string_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/memory:utils_lib",
        "//source/common/network:connection_balancer_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:raw_buffer_socket_lib",
        "//source/common/network:utility_lib",
        "//source/common/router:config_lib",
        "//source/common/router:scoped_config_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/access_loggers/common:file_access_log_lib",
        "//source/extensions/request_id/uuid:config",
        "//source/server:listener_manager_lib",
    ],
)

envoy_cc_library(
    name = "admin_filter_lib",
    srcs = ["admin_filter.cc"],
    hdrs = ["admin_filter.h"],
    deps = [
        ":utils_lib",
        "//envoy/http:filter_interface",
        "//envoy/server:admin_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

genrule(
    name = "generate_admin_html",
    srcs = [
        "admin_head_start.html",
        "admin.css",
    ],
    outs = ["admin_html_gen.h"],
    cmd = "./$(location :generate_admin_html.sh) \
        $(location admin_head_start.html) $(location admin.css) > $@",
    tools = [":generate_admin_html.sh"],
)

envoy_cc_library(
    name = "handler_ctx_lib",
    hdrs = ["handler_ctx.h"],
    deps = [
        "//envoy/server:instance_interface",
    ],
)

envoy_cc_library(
    name = "stats_params_lib",
    srcs = ["stats_params.cc"],
    hdrs = ["stats_params.h"],
    deps = [
        ":utils_lib",
        "//envoy/buffer:buffer_interface",
        "//envoy/http:codes_interface",
        "//envoy/server:admin_interface",
        "//source/common/stats:histogram_lib",
    ],
)

envoy_cc_library(
    name = "stats_request_lib",
    srcs = ["stats_request.cc"],
    hdrs = ["stats_request.h"],
    deps = [
        ":handler_ctx_lib",
        ":prometheus_stats_lib",
        ":stats_params_lib",
        ":stats_render_lib",
        ":utils_lib",
        "//envoy/http:codes_interface",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/stats:histogram_lib",
        "@com_google_absl//absl/container:btree",
    ],
)

envoy_cc_library(
    name = "stats_render_lib",
    srcs = ["stats_render.cc"] + envoy_select_admin_html([
        ":generate_admin_html",
        "stats_html_render.cc",
    ]),
    hdrs = ["stats_render.h"] + envoy_select_admin_html([
        "stats_html_render.h",
    ]),
    deps = [
        ":stats_params_lib",
        ":utils_lib",
        "//envoy/server:admin_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/json:json_sanitizer_lib",
        "//source/common/stats:histogram_lib",
    ] + envoy_select_admin_html([
        "//source/common/html:utility_lib",
    ]),
)

envoy_cc_library(
    name = "stats_handler_lib",
    srcs = ["stats_handler.cc"],
    hdrs = ["stats_handler.h"],
    deps = [
        ":handler_ctx_lib",
        ":prometheus_stats_lib",
        ":stats_render_lib",
        ":stats_request_lib",
        ":utils_lib",
        "//envoy/http:codes_interface",
        "//envoy/server:admin_interface",
        "//envoy/server:instance_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "prometheus_stats_lib",
    srcs = ["prometheus_stats.cc"],
    hdrs = ["prometheus_stats.h"],
    deps = [
        ":stats_params_lib",
        ":utils_lib",
        "//envoy/stats:custom_stat_namespaces_interface",
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
        "//envoy/http:codes_interface",
        "//envoy/server:admin_interface",
        "//envoy/server:instance_interface",
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
        "//envoy/http:codes_interface",
        "//envoy/server:admin_interface",
        "//envoy/server:instance_interface",
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
        "//envoy/http:codes_interface",
        "//envoy/server:admin_interface",
        "//envoy/server:instance_interface",
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
        "//envoy/http:codes_interface",
        "//envoy/server:admin_interface",
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
        "//envoy/http:codes_interface",
        "//envoy/server:admin_interface",
        "//envoy/server:instance_interface",
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
        "//envoy/http:codes_interface",
        "//envoy/server:admin_interface",
        "//envoy/server:instance_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/memory:stats_lib",
        "//source/common/version:version_includes",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "clusters_handler_lib",
    srcs = ["clusters_handler.cc"],
    hdrs = ["clusters_handler.h"],
    deps = [
        ":handler_ctx_lib",
        ":utils_lib",
        "//envoy/http:codes_interface",
        "//envoy/server:admin_interface",
        "//envoy/server:instance_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/upstream:host_utility_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "config_dump_handler_lib",
    srcs = ["config_dump_handler.cc"],
    hdrs = ["config_dump_handler.h"],
    deps = [
        ":config_tracker_lib",
        ":handler_ctx_lib",
        ":utils_lib",
        "//envoy/http:codes_interface",
        "//envoy/server:admin_interface",
        "//envoy/server:instance_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:matchers_lib",
        "//source/common/common:statusor_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "init_dump_handler_lib",
    srcs = ["init_dump_handler.cc"],
    hdrs = ["init_dump_handler.h"],
    deps = [
        ":handler_ctx_lib",
        ":utils_lib",
        "//envoy/server:admin_interface",
        "//envoy/server:instance_interface",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "utils_lib",
    srcs = ["utils.cc"],
    hdrs = ["utils.h"],
    deps = [
        "//envoy/init:manager_interface",
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
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/server:config_tracker_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:macros",
    ],
)
