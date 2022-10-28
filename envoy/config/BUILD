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
        "//envoy/common:time_interface",
        "//source/common/common:assert_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "config_provider_manager_interface",
    hdrs = ["config_provider_manager.h"],
    deps = [
        ":config_provider_interface",
        "//envoy/server:filter_config_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "config_validator_interface",
    hdrs = ["config_validator.h"],
    deps = [
        "//envoy/server:instance_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "context_provider_interface",
    hdrs = ["context_provider.h"],
    deps = [
        "//envoy/common:callback",
        "@com_github_cncf_udpa//xds/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "extension_config_provider_interface",
    hdrs = ["extension_config_provider.h"],
    deps = [
        "//envoy/network:filter_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "grpc_mux_interface",
    hdrs = ["grpc_mux.h"],
    deps = [
        ":subscription_interface",
        "//envoy/stats:stats_macros",
        "//source/common/common:cleanup_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "subscription_factory_interface",
    hdrs = ["subscription_factory.h"],
    deps = [
        ":subscription_interface",
        "@com_github_cncf_udpa//xds/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "subscription_interface",
    hdrs = ["subscription.h"],
    deps = [
        "//envoy/stats:stats_macros",
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

envoy_cc_library(
    name = "xds_resources_delegate_interface",
    hdrs = ["xds_resources_delegate.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":subscription_interface",
        ":typed_config_interface",
        "//envoy/api:api_interface",
        "//envoy/protobuf:message_validator_interface",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)
