licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "admin_lib",
    srcs = ["admin.cc"],
    hdrs = ["admin.h"],
    deps = [
        ":config_tracker_lib",
        "//include/envoy/filesystem:filesystem_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/network:listen_socket_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/server:admin_interface",
        "//include/envoy/server:hot_restart_interface",
        "//include/envoy/server:instance_interface",
        "//include/envoy/server:listener_manager_interface",
        "//include/envoy/server:options_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:resource_manager_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/access_log:access_log_formatter_lib",
        "//source/common/access_log:access_log_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:macros",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/common:version_includes",
        "//source/common/html:utility_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:conn_manager_lib",
        "//source/common/http:date_provider_lib",
        "//source/common/http:default_server_string_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:raw_buffer_socket_lib",
        "//source/common/profiler:profiler_lib",
        "//source/common/router:config_lib",
        "//source/common/stats:stats_lib",
        "//source/common/upstream:host_utility_lib",
        "//source/extensions/access_loggers/file:file_access_log_lib",
        "@envoy_api//envoy/admin/v2alpha:config_dump_cc",
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
