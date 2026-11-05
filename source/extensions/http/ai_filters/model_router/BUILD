load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "quota_interface_lib",
    hdrs = ["quota_interface.h"],
    deps = [
        "//envoy/common:pure_lib",
        "//envoy/thread_local:thread_local_object",
        "//source/common/coroutine:task_lib",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

envoy_cc_library(
    name = "local_quota_lib",
    srcs = ["local_quota.cc"],
    hdrs = ["local_quota.h"],
    deps = [
        ":quota_interface_lib",
        "//envoy/common:time_interface",
        "//source/common/common:token_bucket_impl_lib",
        "//source/common/coroutine:task_lib",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@envoy_api//envoy/extensions/http/ai_filters/model_router/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "async_redis_lib",
    srcs = ["async_redis.cc"],
    hdrs = ["async_redis.h"],
    copts = ["-Wno-old-style-cast"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/event:file_event_interface",
        "//envoy/event:timer_interface",
        "//source/common/common:assert_lib",
        "//source/common/coroutine:leaf_awaitable_lib",
        "//source/common/coroutine:status_macros_lib",
        "//source/common/coroutine:task_lib",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@hiredis",
    ],
)

envoy_cc_library(
    name = "redis_quota_lib",
    srcs = ["redis_quota.cc"],
    hdrs = ["redis_quota.h"],
    deps = [
        ":async_redis_lib",
        ":quota_interface_lib",
        "//envoy/common:time_interface",
        "//envoy/event:dispatcher_interface",
        "//source/common/common:cleanup_lib",
        "//source/common/coroutine:status_macros_lib",
        "//source/common/coroutine:task_lib",
        "//source/common/protobuf:utility_lib",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/http/ai_filters/model_router/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        ":quota_interface_lib",
        "//envoy/api:api_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:logger_lib",
        "//source/common/config:datasource_lib",
        "//source/common/coroutine:status_macros_lib",
        "//source/common/coroutine:task_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_state_lib",
        "//source/extensions/filters/http/ai_protocol_manager:llm_binding_lib",
        "//source/extensions/filters/http/ai_protocol_manager:llm_protocol_conversion_lib",
        "//source/extensions/filters/http/ai_protocol_manager:uri_pattern_lib",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:status_macros",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@envoy_api//envoy/data/ai/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/http/ai_filters/model_router/v3:pkg_cc_proto",
        "@nlohmann_json//:json",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":filter_lib",
        ":local_quota_lib",
        ":quota_interface_lib",
        ":redis_quota_lib",
        "//envoy/registry",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_lib",
        "@abseil-cpp//absl/status:status_macros",
        "@envoy_api//envoy/extensions/http/ai_filters/model_router/v3:pkg_cc_proto",
    ],
)
