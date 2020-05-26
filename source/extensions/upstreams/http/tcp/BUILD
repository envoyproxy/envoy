licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":upstream_request_lib",
    ],
)

# TODO(alyssawilk) clean deps and includes.
envoy_cc_library(
    name = "upstream_request_lib",
    srcs = [
        "upstream_request.cc",
    ],
    hdrs = [
        "upstream_request.h",
    ],
    deps = [
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
        "//source/common/common:cleanup_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:hash_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:linked_object",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:scope_tracker",
        "//source/common/common:utility_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/network:application_protocol_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/router:router_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/stream_info:uint32_accessor_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/extensions/common/proxy_protocol:proxy_protocol_header_lib",
    ],
)
