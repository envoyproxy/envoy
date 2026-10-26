load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "llm_protocol_detection_lib",
    srcs = ["llm_protocol_detection.cc"],
    hdrs = ["llm_protocol_detection.h"],
    deps = [
        "//envoy/http:header_map_interface",
        "//source/common/common:macros",
        "//source/common/http:path_utility_lib",
        "//source/extensions/filters/http/ai_protocol_manager:json_readers_lib",
        "//source/extensions/filters/http/ai_protocol_manager:token_usage_lib",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@nlohmann_json//:json",
    ],
)

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        ":llm_protocol_detection_lib",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/common:logger_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_lib",
        "//source/extensions/filters/http/ai_protocol_manager:llm_protocol_adapters_lib",
        "//source/extensions/filters/http/ai_protocol_manager:llm_protocol_conversion_lib",
        "//source/extensions/filters/http/ai_protocol_manager:schema_lib",
        "//source/extensions/filters/http/ai_protocol_manager:token_usage_lib",
        "//source/extensions/http/ai_filters/common:sync_filter_lib",
        "@abseil-cpp//absl/status",
        "@envoy_api//envoy/extensions/http/ai_filters/schema_validation/v3:pkg_cc_proto",
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
        "@envoy_api//envoy/extensions/http/ai_filters/schema_validation/v3:pkg_cc_proto",
    ],
)
