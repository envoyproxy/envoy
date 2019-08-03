licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "config_provider_interface",
    hdrs = ["config_provider.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/common:time_interface",
        "//source/common/common:assert_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "config_provider_manager_interface",
    hdrs = ["config_provider_manager.h"],
    deps = [
        ":config_provider_interface",
        "//include/envoy/server:filter_config_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "grpc_mux_interface",
    hdrs = ["grpc_mux.h"],
    deps = [
        ":subscription_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "subscription_factory_interface",
    hdrs = ["subscription_factory.h"],
    deps = [
        ":subscription_interface",
        "@envoy_api//envoy/api/v2/core:config_source_cc",
    ],
)

envoy_cc_library(
    name = "subscription_interface",
    hdrs = ["subscription.h"],
    deps = [
        "//include/envoy/stats:stats_macros",
        "//source/common/protobuf",
        "@envoy_api//envoy/api/v2:discovery_cc",
    ],
)

envoy_cc_library(
    name = "typed_metadata_interface",
    hdrs = ["typed_metadata.h"],
    deps = [
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "xds_grpc_context_interface",
    hdrs = ["xds_grpc_context.h"],
    deps = [
        ":subscription_interface",
        "//source/common/protobuf",
    ],
)
