licenses(["notice"])  # Apache 2
# L4 network filter that implements HTTP protocol handling and filtering. This filter internally
# drives all of the L7 HTTP filters.
# Public docs: docs/root/configuration/http_conn_man/http_conn_man.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
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
        "//source/common/http:conn_manager_lib",
        "//source/common/http:default_server_string_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/http/http2:codec_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:rds_lib",
        "//source/extensions/filters/network:well_known_names",
    ],
)
