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
        "@abseil-cpp//absl/strings",
        "@nlohmann_json//:json",
    ],
)

envoy_cc_library(
    name = "ai_session_lib",
    hdrs = ["ai_session.h"],
    deps = [
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:optional",
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
        "@abseil-cpp//absl/strings",
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
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/strings",
    ],
)

envoy_cc_library(
    name = "example_filters_lib",
    hdrs = ["example_filters.h"],
    deps = [
        ":ai_filter_lib",
        ":ai_session_lib",
        "@abseil-cpp//absl/strings",
        "@nlohmann_json//:json",
    ],
)

# ---------------------------------------------------------------------------
# ai_filter_config_factory_lib
#
# NamedAiFilterConfigFactory base interface, extending Config::TypedFactory.
# Depend on this in factory implementations and in the ai_session config.
# ---------------------------------------------------------------------------
envoy_cc_library(
    name = "ai_filter_config_factory_lib",
    hdrs = ["ai_filter_config_factory.h"],
    deps = [
        ":ai_session_manager_lib",
        "//envoy/config:typed_config_interface",
        "//envoy/registry",
        "@abseil-cpp//absl/strings",
    ],
)

# ---------------------------------------------------------------------------
# mcp_filter_factories_lib
#
# Registers envoy.ai_filters.mcp_auth / mcp_init / mcp_context via
# REGISTER_FACTORY, each with a typed config proto (McpAuthConfig, etc.).
# Must be linked into any binary that loads envoy.filters.http.ai_session.
# ---------------------------------------------------------------------------
envoy_cc_library(
    name = "mcp_filter_factories_lib",
    srcs = ["mcp_filter_factories.cc"],
    deps = [
        ":ai_filter_config_factory_lib",
        ":example_filters_lib",
        "//envoy/registry",
        "@envoy_api//envoy/extensions/filters/http/ai_session/v3:pkg_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":ai_filter_config_factory_lib",
        ":ai_session_manager_lib",
        ":mcp_filter_factories_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:assert_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/json_rpc:json_rpc_connection_manager_lib",
        "@envoy_api//envoy/extensions/filters/http/ai_session/v3:pkg_cc_proto",
        "@abseil-cpp//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)
