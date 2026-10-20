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
        "//source/common/http:utility_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_state_lib",
        "//source/extensions/filters/http/ai_protocol_manager:uri_pattern_lib",
        "//source/extensions/http/ai_filters/common:sync_filter_lib",
        "//source/extensions/http/ai_filters/model_router:filter_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@envoy_api//envoy/data/ai/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/http/ai_filters/route_selector/v3:pkg_cc_proto",
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
        "@envoy_api//envoy/extensions/http/ai_filters/route_selector/v3:pkg_cc_proto",
    ],
)
