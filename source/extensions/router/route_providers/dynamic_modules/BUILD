load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Dynamic modules route provider.

envoy_extension_package()

envoy_cc_library(
    name = "route_provider_lib",
    srcs = [
        "abi_impl.cc",
        "route_provider.cc",
    ],
    hdrs = ["route_provider.h"],
    deps = [
        "//envoy/http:codes_interface",
        "//envoy/init:manager_interface",
        "//envoy/router:route_producer_interface",
        "//envoy/router:router_interface",
        "//envoy/server:factory_context_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:statusor_lib",
        "//source/common/config:metadata_lib",
        "//source/common/config:well_known_names",
        "//source/common/http:hash_policy_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:config_lib",
        "//source/common/router:delegating_route_lib",
        "//source/common/router:metadatamatchcriteria_lib",
        "//source/common/router:per_filter_config_lib",
        "//source/common/router:retry_policy_lib",
        "//source/common/router:router_ratelimit_lib",
        "//source/extensions/dynamic_modules:abi_context_accessors_lib",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/router/route_providers/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":route_provider_lib",
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/router/route_providers/dynamic_modules/v3:pkg_cc_proto",
    ],
)
