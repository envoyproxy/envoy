load(
    "@envoy_build_config//:extensions_build_config.bzl",
    "LEGACY_ALWAYSLINK",
)
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_google_grpc_external_deps",
    "envoy_package",
    "envoy_select_google_grpc",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "typed_async_client_lib",
    srcs = ["typed_async_client.cc"],
    hdrs = ["typed_async_client.h"],
    deps = [
        ":codec_lib",
        ":context_lib",
        "//envoy/grpc:async_client_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/http:async_client_lib",
    ],
)

envoy_cc_library(
    name = "async_client_lib",
    srcs = ["async_client_impl.cc"],
    hdrs = ["async_client_impl.h"],
    deps = [
        ":codec_lib",
        ":context_lib",
        ":typed_async_client_lib",
        "//envoy/grpc:async_client_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/http:async_client_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "async_client_manager_lib",
    srcs = ["async_client_manager_impl.cc"],
    hdrs = ["async_client_manager_impl.h"],
    deps = [
        ":async_client_lib",
        ":context_lib",
        "//envoy/grpc:async_client_manager_interface",
        "//envoy/singleton:manager_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/upstream:cluster_manager_interface",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ] + envoy_select_google_grpc([":google_async_client_lib"]),
)

envoy_cc_library(
    name = "codec_lib",
    srcs = ["codec.cc"],
    hdrs = ["codec.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "status_lib",
    srcs = ["status.cc"],
    hdrs = ["status.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//envoy/grpc:status",
    ],
)

envoy_cc_library(
    name = "common_lib",
    srcs = ["common.cc"],
    hdrs = ["common.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//envoy/http:header_map_interface",
        "//envoy/http:message_interface",
        "//envoy/stats:stats_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:base64_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:hash_lib",
        "//source/common/common:macros",
        "//source/common/common:safe_memcpy_lib",
        "//source/common/common:utility_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:status_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "context_lib",
    srcs = ["context_impl.cc"],
    hdrs = ["context_impl.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":common_lib",
        ":stat_names_lib",
        "//envoy/grpc:context_interface",
        "//envoy/http:header_map_interface",
        "//envoy/stats:stats_interface",
        "//source/common/common:hash_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
    ],
)

envoy_cc_library(
    name = "google_grpc_utils_lib",
    srcs = ["google_grpc_utils.cc"],
    hdrs = ["google_grpc_utils.h"],
    external_deps = [
        "abseil_optional",
        "grpc",
    ],
    deps = [
        ":google_grpc_creds_lib",
        "//envoy/api:api_interface",
        "//envoy/registry",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/common:macros",
        "//source/common/common:utility_lib",
        "//source/common/grpc:status_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "stat_names_lib",
    srcs = ["stat_names.cc"],
    hdrs = ["stat_names.h"],
    deps = [
        "//envoy/grpc:status",
        "//source/common/stats:symbol_table_lib",
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
        ":context_lib",
        ":google_grpc_context_lib",
        ":google_grpc_creds_lib",
        ":google_grpc_utils_lib",
        ":stat_names_lib",
        ":typed_async_client_lib",
        "//envoy/api:api_interface",
        "//envoy/grpc:google_grpc_creds_interface",
        "//envoy/stream_info:stream_info_interface",
        "//envoy/thread:thread_interface",
        "//envoy/thread_local:thread_local_object",
        "//source/common/common:base64_lib",
        "//source/common/common:empty_string",
        "//source/common/common:linked_object",
        "//source/common/common:thread_annotations",
        "//source/common/tracing:http_tracer_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "google_grpc_context_lib",
    srcs = ["google_grpc_context.cc"],
    hdrs = ["google_grpc_context.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:lock_guard_lib",
        "//source/common/common:macros",
        "//source/common/common:thread_lib",
    ] + envoy_google_grpc_external_deps(),
)

envoy_cc_library(
    name = "google_grpc_creds_lib",
    srcs = ["google_grpc_creds_impl.cc"],
    hdrs = ["google_grpc_creds_impl.h"],
    external_deps = ["grpc"],
    deps = [
        "//envoy/api:api_interface",
        "//envoy/grpc:google_grpc_creds_interface",
        "//envoy/registry",
        "//source/common/config:datasource_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_library(
    name = "buffered_async_client_lib",
    hdrs = ["buffered_async_client.h"],
    deps = [
        ":buffered_message_ttl_manager_lib",
        ":typed_async_client_lib",
        "//source/common/protobuf:utility_lib",
        "@com_google_absl//absl/container:btree",
    ],
)

envoy_cc_library(
    name = "buffered_message_ttl_manager_lib",
    hdrs = ["buffered_message_ttl_manager.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
    ],
)
