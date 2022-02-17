load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "null_span_lib",
    hdrs = [
        "null_span_impl.h",
    ],
    deps = [
        "//envoy/tracing:trace_driver_interface",
        "//source/common/common:empty_string",
    ],
)

envoy_cc_library(
    name = "common_values_lib",
    hdrs = [
        "common_values.h",
    ],
    deps = [
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "http_tracer_lib",
    srcs = [
        "http_tracer_impl.cc",
    ],
    hdrs = [
        "http_tracer_impl.h",
    ],
    deps = [
        ":common_values_lib",
        ":null_span_lib",
        "//envoy/http:request_id_extension_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/tracing:http_tracer_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/common:base64_lib",
        "//source/common/common:macros",
        "//source/common/common:utility_lib",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/stream_info:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/metadata/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/tracing/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "tracer_config_lib",
    hdrs = [
        "tracer_config_impl.h",
    ],
    deps = [
        "//envoy/server:tracer_config_interface",
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
        "//envoy/server:tracer_config_interface",
        "//envoy/singleton:instance_interface",
        "//envoy/tracing:http_tracer_manager_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/tracing:http_tracer_lib",
    ],
)

envoy_cc_library(
    name = "custom_tag_lib",
    srcs = [
        "custom_tag_impl.cc",
    ],
    hdrs = [
        "custom_tag_impl.h",
    ],
    deps = [
        "//envoy/router:router_interface",
        "//envoy/tracing:custom_tag_interface",
        "//source/common/config:metadata_lib",
        "@envoy_api//envoy/type/tracing/v3:pkg_cc_proto",
    ],
)
