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
        "//source/common/http/filter:buffer_filter_lib",
        "//source/common/json:config_schemas_lib",
    ],
)

envoy_cc_library(
    name = "dynamo_lib",
    srcs = ["dynamo.cc"],
    hdrs = ["dynamo.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
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
        "//source/common/http/filter:fault_filter_lib",
        "//source/common/json:config_schemas_lib",
    ],
)

envoy_cc_library(
    name = "grpc_http1_bridge_lib",
    srcs = ["grpc_http1_bridge.cc"],
    hdrs = ["grpc_http1_bridge.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/grpc:http1_bridge_filter_lib",
    ],
)

envoy_cc_library(
    name = "grpc_web_lib",
    srcs = ["grpc_web.cc"],
    hdrs = ["grpc_web.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
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
        "//source/common/http/filter:ip_tagging_filter_lib",
        "//source/common/json:config_schemas_lib",
    ],
)

envoy_cc_library(
    name = "lightstep_lib",
    srcs = ["lightstep_http_tracer.cc"],
    hdrs = ["lightstep_http_tracer.h"],
    deps = [
        "//source/common/tracing:http_tracer_lib",
        "//source/common/tracing:lightstep_tracer_lib",
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
        "//source/common/http/filter:ratelimit_includes",
        "//source/common/http/filter:ratelimit_lib",
    ],
)

envoy_cc_library(
    name = "router_lib",
    srcs = ["router.cc"],
    hdrs = ["router.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/json:config_schemas_lib",
        "//source/common/router:router_lib",
        "//source/common/router:shadow_writer_lib",
    ],
)

envoy_cc_library(
    name = "zipkin_lib",
    srcs = ["zipkin_http_tracer.cc"],
    hdrs = ["zipkin_http_tracer.h"],
    deps = [
        "//source/common/tracing/zipkin:zipkin_lib",
        "//source/server:configuration_lib",
    ],
)
