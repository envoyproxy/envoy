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
        "//source/common/config:metadata_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:uuid_util_lib",
        "//source/common/stream_info:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/metadata/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/tracing/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "http_tracer_config_lib",
    hdrs = [
        "http_tracer_config_impl.h",
    ],
    deps = [
        "//include/envoy/server:tracer_config_interface",
    ],
)

envoy_cc_library(
    name = "http_tracer_manager_lib",
    srcs = [
        "http_tracer_manager_impl.cc",
    ],
    hdrs = [
        "http_tracer_manager_impl.h",
    ],
    deps = [
        "//include/envoy/server:tracer_config_interface",
        "//include/envoy/singleton:instance_interface",
        "//include/envoy/tracing:http_tracer_manager_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/tracing:http_tracer_lib",
    ],
)
