load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

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
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "subscription_interface",
    hdrs = ["subscription.h"],
    deps = [
        "//include/envoy/stats:stats_macros",
        "//source/common/config:api_type_oracle_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "typed_config_interface",
    hdrs = ["typed_config.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "typed_metadata_interface",
    hdrs = ["typed_metadata.h"],
    deps = [
        ":typed_config_interface",
        "//source/common/protobuf",
    ],
)
