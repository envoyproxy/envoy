load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "json_pointer_lib",
    srcs = ["json_pointer.cc"],
    hdrs = ["json_pointer.h"],
    deps = [
        "//source/extensions/filters/http/ai_protocol_manager:json_with_ext_buf_lib",
        "@abseil-cpp//absl/algorithm:container",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@nlohmann_json//:json",
    ],
)

envoy_cc_library(
    name = "ai_filter_lib",
    srcs = [
        "abi_impl.cc",
        "ai_filter.cc",
    ],
    hdrs = ["ai_filter.h"],
    deps = [
        ":json_pointer_lib",
        "//envoy/common:exception_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:dispatcher_thread_deletable",
        "//envoy/http:codes_interface",
        "//envoy/http:header_map_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/extensions/dynamic_modules:abi_context_accessors_lib",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_request_lib",
        "//source/extensions/filters/http/ai_protocol_manager:token_usage_lib",
        "//source/extensions/http/ai_filters/common:sync_filter_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":ai_filter_lib",
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/http/ai_filters/dynamic_modules/v3:pkg_cc_proto",
    ],
)
