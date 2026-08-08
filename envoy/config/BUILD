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
    deps = [
        "//envoy/common:pure_lib",
        "//envoy/common:time_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "config_provider_manager_interface",
    hdrs = ["config_provider_manager.h"],
    deps = [
        ":config_provider_interface",
        "//envoy/common:pure_lib",
        "//envoy/init:manager_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/singleton:instance_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "config_validator_interface",
    hdrs = ["config_validator.h"],
    deps = [
        ":subscription_interface",
        ":typed_config_interface",
        "//envoy/common:pure_lib",
        "//envoy/protobuf:message_validator_interface",
        "//envoy/server:instance_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "context_provider_interface",
    hdrs = ["context_provider.h"],
    deps = [
        "//envoy/common:callback",
        "//envoy/common:pure_lib",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/strings",
        "@xds//xds/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "extension_config_provider_interface",
    hdrs = ["extension_config_provider.h"],
    deps = [
        "//envoy/common:optref_lib",
        "//envoy/common:pure_lib",
    ],
)

envoy_cc_library(
    name = "dynamic_extension_config_provider_interface",
    hdrs = ["dynamic_extension_config_provider.h"],
    deps = [
        ":extension_config_provider_interface",
        "//envoy/common:pure_lib",
        "//envoy/network:filter_interface",
        "//source/common/protobuf",
        "@abseil-cpp//absl/status",
    ],
)

envoy_cc_library(
    name = "grpc_mux_interface",
    hdrs = ["grpc_mux.h"],
    deps = [
        ":eds_resources_cache_interface",
        ":subscription_interface",
        "//envoy/common:backoff_strategy_interface",
        "//envoy/common:pure_lib",
        "//envoy/grpc:async_client_interface",
        "//envoy/stats:scope_interface",
        "//envoy/stats:stats_macros",
        "//envoy/upstream:load_stats_reporter_interface",
        "//source/common/common:cleanup_lib",
        "//source/common/protobuf:arena_wrapped_proto_lib",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "custom_config_validators_interface",
    hdrs = ["custom_config_validators.h"],
    deps = [
        ":subscription_interface",
        "//envoy/common:pure_lib",
        "//source/common/protobuf",
        "@abseil-cpp//absl/strings",
    ],
)

envoy_cc_library(
    name = "subscription_factory_interface",
    hdrs = ["subscription_factory.h"],
    deps = [
        ":custom_config_validators_interface",
        ":subscription_interface",
        "//envoy/common:backoff_strategy_interface",
        "//envoy/upstream:load_stats_reporter_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@xds//xds/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "subscription_interface",
    hdrs = ["subscription.h"],
    deps = [
        "//envoy/common:exception_lib",
        "//envoy/common:optref_lib",
        "//envoy/common:pure_lib",
        "//envoy/stats:stats_macros",
        "//source/common/protobuf",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/types:span",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "typed_config_interface",
    hdrs = ["typed_config.h"],
    deps = [
        "//envoy/common:pure_lib",
        "//source/common/common:assert_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "typed_metadata_interface",
    hdrs = ["typed_metadata.h"],
    deps = [
        ":typed_config_interface",
        "//envoy/common:pure_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "xds_resources_delegate_interface",
    hdrs = ["xds_resources_delegate.h"],
    deps = [
        ":subscription_interface",
        ":typed_config_interface",
        "//envoy/api:api_interface",
        "//envoy/common:exception_lib",
        "//envoy/common:optref_lib",
        "//envoy/common:pure_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/protobuf:message_validator_interface",
        "//source/common/protobuf",
        "@abseil-cpp//absl/container:flat_hash_set",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "xds_config_tracker_interface",
    hdrs = ["xds_config_tracker.h"],
    deps = [
        ":subscription_interface",
        ":typed_config_interface",
        "//envoy/api:api_interface",
        "//envoy/common:optref_lib",
        "//envoy/common:pure_lib",
        "//envoy/protobuf:message_validator_interface",
        "//source/common/protobuf",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
        "@googleapis//google/rpc:status_cc_proto",
    ],
)

envoy_cc_library(
    name = "eds_resources_cache_interface",
    hdrs = ["eds_resources_cache.h"],
    deps = [
        "//envoy/common:optref_lib",
        "//envoy/common:pure_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "xds_manager_interface",
    hdrs = ["xds_manager.h"],
    deps = [
        ":grpc_mux_interface",
        ":subscription_factory_interface",
        ":subscription_interface",
        "//envoy/common:optref_lib",
        "//envoy/common:pure_lib",
        "//envoy/stats:scope_interface",
        "//envoy/upstream:cluster_manager_interface",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
