licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "http_connection_manager_lib",
    srcs = ["http_connection_manager.cc"],
    hdrs = ["http_connection_manager.h"],
    deps = [
        "//include/envoy/filesystem:filesystem_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/router:route_config_provider_manager_interface",
        "//include/envoy/server:admin_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/server:options_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/common:logger_lib",
        "//source/common/config:filter_json_lib",
        "//source/common/config:utility_lib",
        "//source/common/config:well_known_names",
        "//source/common/http:conn_manager_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/http/http2:codec_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:rds_lib",
    ],
)

envoy_cc_library(
    name = "raw_buffer_socket_lib",
    srcs = ["raw_buffer_socket.cc"],
    hdrs = ["raw_buffer_socket.h"],
    deps = [
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/registry",
        "//include/envoy/server:transport_socket_config_interface",
        "//source/common/config:well_known_names",
        "//source/common/network:raw_buffer_socket_lib",
    ],
)

envoy_cc_library(
    name = "ssl_socket_lib",
    srcs = ["ssl_socket.cc"],
    hdrs = ["ssl_socket.h"],
    deps = [
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/registry",
        "//include/envoy/server:transport_socket_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/config:well_known_names",
        "//source/common/protobuf:utility_lib",
        "//source/common/ssl:context_config_lib",
        "//source/common/ssl:ssl_socket_lib",
        "@envoy_api//envoy/api/v2/auth:cert_cc",
    ],
)
