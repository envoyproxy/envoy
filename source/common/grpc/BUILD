licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_google_grpc",
)

envoy_package()

envoy_cc_library(
    name = "async_client_lib",
    srcs = ["async_client_impl.cc"],
    hdrs = ["async_client_impl.h"],
    deps = [
        ":codec_lib",
        ":common_lib",
        "//include/envoy/grpc:async_client_interface",
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/http:async_client_lib",
    ],
)

envoy_cc_library(
    name = "async_client_manager_lib",
    srcs = ["async_client_manager_impl.cc"],
    hdrs = ["async_client_manager_impl.h"],
    deps = [
        ":async_client_lib",
        "//include/envoy/grpc:async_client_manager_interface",
        "//include/envoy/singleton:manager_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/upstream:cluster_manager_interface",
    ] + envoy_select_google_grpc([":google_async_client_lib"]),
)

envoy_cc_library(
    name = "codec_lib",
    srcs = ["codec.cc"],
    hdrs = ["codec.h"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "common_lib",
    srcs = ["common.cc"],
    hdrs = ["common.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/grpc:status",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/http:message_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:macros",
        "//source/common/common:utility_lib",
        "//source/common/http:filter_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "google_async_client_lib",
    srcs = ["google_async_client_impl.cc"],
    hdrs = ["google_async_client_impl.h"],
    external_deps = [
        "abseil_synchronization",
        "grpc",
    ],
    deps = [
        ":google_async_site_lib",
        "//include/envoy/grpc:async_client_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//source/common/common:empty_string",
        "//source/common/common:linked_object",
        "//source/common/common:thread_annotations",
        "//source/common/common:thread_lib",
        "//source/common/config:datasource_lib",
        "//source/common/tracing:http_tracer_lib",
    ],
)

envoy_cc_library(
    name = "google_async_site_lib",
    srcs = ["google_async_site_impl.cc"],
    deps = ["//include/envoy/grpc:google_async_site_interface"],
)
