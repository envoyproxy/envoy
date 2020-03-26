licenses(["notice"])  # Apache 2

# L4 network filter that implements HTTP protocol handling and filtering. This filter internally
# drives all of the L7 HTTP filters.
# Public docs: docs/root/configuration/http_conn_man/http_conn_man.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_package",
)

envoy_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        "//include/envoy/config:config_provider_manager_interface",
        "//include/envoy/filesystem:filesystem_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/router:route_config_provider_manager_interface",
        "//include/envoy/server:admin_interface",
        "//include/envoy/server:options_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:conn_manager_lib",
        "//source/common/http:default_server_string_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/http/http2:codec_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:rds_lib",
        "//source/common/router:scoped_rds_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/tracing:http_tracer_config_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/common/tracing:http_tracer_manager_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/tracing/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)
