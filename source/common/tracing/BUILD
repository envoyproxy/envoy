licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "http_tracer_lib",
    srcs = [
        "http_tracer_impl.cc",
    ],
    hdrs = [
        "http_tracer_impl.h",
    ],
    deps = [
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/access_log:access_log_formatter_lib",
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/common:base64_lib",
        "//source/common/common:macros",
        "//source/common/common:utility_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/request_info:utility_lib",
        "//source/common/runtime:uuid_util_lib",
    ],
)

envoy_cc_library(
    name = "opentracing_driver_lib",
    srcs = [
        "opentracing_driver_impl.cc",
    ],
    hdrs = [
        "opentracing_driver_impl.h",
    ],
    external_deps = ["opentracing"],
    deps = [
        ":http_tracer_lib",
    ],
)

envoy_cc_library(
    name = "dynamic_opentracing_driver_lib",
    srcs = [
        "dynamic_opentracing_driver_impl.cc",
    ],
    hdrs = [
        "dynamic_opentracing_driver_impl.h",
    ],
    deps = [
        ":http_tracer_lib",
        ":opentracing_driver_lib",
    ],
)

envoy_cc_library(
    name = "lightstep_tracer_lib",
    srcs = [
        "lightstep_tracer_impl.cc",
    ],
    hdrs = [
        "lightstep_tracer_impl.h",
    ],
    external_deps = ["lightstep"],
    deps = [
        ":http_tracer_lib",
        ":opentracing_driver_lib",
    ],
)
