licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_library",
    "envoy_package",
)
load(
    "//source/extensions:all_extensions.bzl",
    "envoy_all_extensions",
)

envoy_package()

alias(
    name = "envoy",
    actual = ":envoy-static",
)

envoy_cc_binary(
    name = "envoy-static",
    stamped = True,
    deps = ["envoy_main_entry_lib"],
)

envoy_cc_library(
    name = "envoy_common_lib",
    deps = [
        "//source/common/compressor:compressor_lib",
        "//source/common/decompressor:decompressor_lib",
        "//source/common/event:libevent_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/server:drain_manager_lib",
        "//source/server:options_lib",
        "//source/server:server_lib",
        "//source/server:test_hooks_lib",
        "//source/server/config/access_log:file_access_log_lib",
        "//source/server/config/access_log:grpc_access_log_lib",
        "//source/server/config/http:buffer_lib",
        "//source/server/config/http:cors_lib",
        "//source/server/config/http:fault_lib",
        "//source/server/config/http:grpc_http1_bridge_lib",
        "//source/server/config/http:grpc_json_transcoder_lib",
        "//source/server/config/http:grpc_web_lib",
        "//source/server/config/http:gzip_lib",
        "//source/server/config/http:ip_tagging_lib",
        "//source/server/config/http:lua_lib",
        "//source/server/config/http:router_lib",
        "//source/server/config/listener:original_dst_lib",
        "//source/server/config/listener:proxy_protocol_lib",
        "//source/server/config/network:http_connection_manager_lib",
        "//source/server/config/network:raw_buffer_socket_lib",
        "//source/server/config/network:ssl_socket_lib",
        "//source/server/config/stats:dog_statsd_lib",
        "//source/server/config/stats:metrics_service_lib",
        "//source/server/config/stats:statsd_lib",
        "//source/server/http:health_check_lib",
    ] + envoy_all_extensions(),
)

# Non-core HTTP/TCP proxy libraries.
# TODO(mattklein123): Delete this when extension migration is complete.
envoy_cc_library(
    name = "extra_protocol_proxies_lib",
    deps = [
        "//source/server/config/http:dynamo_lib",
    ],
)

envoy_cc_library(
    name = "envoy_main_entry_lib",
    srcs = ["main.cc"],
    deps = [
        ":envoy_main_common_lib",
        ":extra_protocol_proxies_lib",
        "//source/server/config/http:dynamic_opentracing_lib",
        "//source/server/config/http:lightstep_lib",
        "//source/server/config/http:zipkin_lib",
    ],
)

envoy_cc_library(
    name = "envoy_main_common_lib",
    srcs = ["main_common.cc"],
    hdrs = ["main_common.h"],
    deps = [
        ":envoy_common_lib",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:compiler_requirements_lib",
        "//source/common/common:perf_annotation_lib",
        "//source/server:hot_restart_lib",
        "//source/server:hot_restart_nop_lib",
        "//source/server:proto_descriptors_lib",
        "//source/server/config_validation:server_lib",
    ] + select({
        "//bazel:disable_signal_trace": [],
        "//conditions:default": [":sigaction_lib"],
    }),
)

envoy_cc_library(
    name = "sigaction_lib",
    srcs = ["signal_action.cc"],
    hdrs = ["signal_action.h"],
    tags = ["backtrace"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:non_copyable",
        "//source/server:backtrace_lib",
    ],
)
