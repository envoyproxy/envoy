load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "ai_filter_lib",
    hdrs = ["ai_filter.h"],
    deps = [
        "@com_google_absl//absl/strings",
        "@nlohmann_json//:json",
    ],
)

envoy_cc_library(
    name = "ai_session_lib",
    hdrs = ["ai_session.h"],
    deps = [
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
        "@nlohmann_json//:json",
    ],
)

envoy_cc_library(
    name = "ai_filter_chain_lib",
    srcs = ["ai_filter_chain.cc"],
    hdrs = ["ai_filter_chain.h"],
    deps = [
        ":ai_filter_lib",
        ":ai_session_lib",
        "//envoy/http:filter_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/extensions/filters/http/json_rpc:json_rpc_decoder_lib",
        "@com_google_absl//absl/strings",
        "@nlohmann_json//:json",
    ],
)

envoy_cc_library(
    name = "ai_session_manager_lib",
    srcs = ["ai_session_manager.cc"],
    hdrs = ["ai_session_manager.h"],
    deps = [
        ":ai_filter_chain_lib",
        ":ai_filter_lib",
        ":ai_session_lib",
        "//envoy/http:filter_interface",
        "//envoy/http:header_map_interface",
        "//source/common/common:logger_lib",
        "//source/extensions/filters/http/json_rpc:json_rpc_connection_manager_lib",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_library(
    name = "example_filters_lib",
    hdrs = ["example_filters.h"],
    deps = [
        ":ai_filter_lib",
        ":ai_session_lib",
        "@com_google_absl//absl/strings",
        "@nlohmann_json//:json",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":ai_session_manager_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/extensions/filters/http/ai_session:ai_filter_lib",
        "//source/extensions/filters/http/ai_session:example_filters_lib",
        "//source/extensions/filters/http/json_rpc:json_rpc_connection_manager_lib",
        "@com_google_protobuf//:protobuf",
    ],
)
