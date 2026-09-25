load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/common:logger_lib",
        "//source/common/coroutine:status_macros_lib",
        "//source/common/coroutine:task_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_lib",
        "//source/extensions/filters/http/ai_protocol_manager:json_readers_lib",
        "//source/extensions/filters/http/ai_protocol_manager:json_with_ext_buf_lib",
        "//source/extensions/filters/http/ai_protocol_manager:transcoding_engine_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/http/ai_filters/transcoder/v3:pkg_cc_proto",
        "@nlohmann_json//:json",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":filter_lib",
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_lib",
        "//source/extensions/filters/http/ai_protocol_manager:transcoding_engine_lib",
        "@envoy_api//envoy/extensions/http/ai_filters/transcoder/v3:pkg_cc_proto",
    ],
)
