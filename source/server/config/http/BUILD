licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "buffer_lib",
    srcs = ["buffer.cc"],
    hdrs = ["buffer.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
        "//source/common/http/filter:buffer_filter_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "lua_lib",
    srcs = ["lua.cc"],
    hdrs = ["lua.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
        "//source/common/http/filter/lua:lua_filter_lib",
    ],
)

envoy_cc_library(
    name = "empty_http_filter_config_lib",
    hdrs = ["empty_http_filter_config.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:well_known_names",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "cors_lib",
    srcs = ["cors.cc"],
    hdrs = ["cors.h"],
    deps = [
        ":empty_http_filter_config_lib",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:well_known_names",
        "//source/common/http/filter:cors_filter_lib",
    ],
)

envoy_cc_library(
    name = "dynamo_lib",
    srcs = ["dynamo.cc"],
    hdrs = ["dynamo.h"],
    deps = [
        ":empty_http_filter_config_lib",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:well_known_names",
        "//source/common/dynamo:dynamo_filter_lib",
    ],
)

envoy_cc_library(
    name = "fault_lib",
    srcs = ["fault.cc"],
    hdrs = ["fault.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
        "//source/common/http/filter:fault_filter_lib",
    ],
)

envoy_cc_library(
    name = "grpc_http1_bridge_lib",
    srcs = ["grpc_http1_bridge.cc"],
    hdrs = ["grpc_http1_bridge.h"],
    deps = [
        ":empty_http_filter_config_lib",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:well_known_names",
        "//source/common/grpc:http1_bridge_filter_lib",
    ],
)

envoy_cc_library(
    name = "grpc_json_transcoder_lib",
    srcs = ["grpc_json_transcoder.cc"],
    hdrs = ["grpc_json_transcoder.h"],
    deps = [
        "//include/envoy/server:instance_interface",
        "//source/common/config:well_known_names",
        "//source/common/grpc:json_transcoder_filter_lib",
        "//source/server/config/network:http_connection_manager_lib",
    ],
)

envoy_cc_library(
    name = "gzip_lib",
    srcs = ["gzip.cc"],
    hdrs = ["gzip.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:well_known_names",
        "//source/common/http/filter:gzip_filter_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "grpc_web_lib",
    srcs = ["grpc_web.cc"],
    hdrs = ["grpc_web.h"],
    deps = [
        ":empty_http_filter_config_lib",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:well_known_names",
        "//source/common/grpc:grpc_web_filter_lib",
    ],
)

envoy_cc_library(
    name = "ip_tagging_lib",
    srcs = ["ip_tagging.cc"],
    hdrs = ["ip_tagging.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
        "//source/common/http/filter:ip_tagging_filter_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/filter/http/ip_tagging/v2:ip_tagging_cc",
    ],
)

envoy_cc_library(
    name = "lightstep_lib",
    srcs = ["lightstep_http_tracer.cc"],
    hdrs = ["lightstep_http_tracer.h"],
    deps = [
        "//source/common/config:well_known_names",
        "//source/common/tracing:http_tracer_lib",
        "//source/common/tracing:lightstep_tracer_lib",
        "//source/server:configuration_lib",
    ],
)

envoy_cc_library(
    name = "dynamic_opentracing_lib",
    srcs = ["dynamic_opentracing_http_tracer.cc"],
    hdrs = ["dynamic_opentracing_http_tracer.h"],
    deps = [
        "//source/common/config:well_known_names",
        "//source/common/tracing:dynamic_opentracing_driver_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/server:configuration_lib",
    ],
)

envoy_cc_library(
    name = "ratelimit_lib",
    srcs = ["ratelimit.cc"],
    hdrs = ["ratelimit.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
        "//source/common/http/filter:ratelimit_includes",
        "//source/common/http/filter:ratelimit_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "router_lib",
    srcs = ["router.cc"],
    hdrs = ["router.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
        "//source/common/json:config_schemas_lib",
        "//source/common/router:router_lib",
        "//source/common/router:shadow_writer_lib",
        "@envoy_api//envoy/config/filter/http/router/v2:router_cc",
    ],
)

envoy_cc_library(
    name = "squash_lib",
    srcs = ["squash.cc"],
    hdrs = ["squash.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
        "//source/common/http/filter:squash_filter_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "zipkin_lib",
    srcs = ["zipkin_http_tracer.cc"],
    hdrs = ["zipkin_http_tracer.h"],
    deps = [
        "//source/common/config:well_known_names",
        "//source/common/tracing/zipkin:zipkin_lib",
        "//source/server:configuration_lib",
    ],
)
