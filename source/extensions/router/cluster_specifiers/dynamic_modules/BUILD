load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Dynamic modules cluster specifier plugin.

envoy_extension_package()

envoy_cc_library(
    name = "cluster_specifier_lib",
    srcs = [
        "abi_impl.cc",
        "cluster_specifier.cc",
    ],
    hdrs = ["cluster_specifier.h"],
    deps = [
        "//envoy/common:exception_lib",
        "//envoy/router:cluster_specifier_plugin_interface",
        "//envoy/server:factory_context_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:statusor_lib",
        "//source/common/config:well_known_names",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:config_lib",
        "//source/common/router:delegating_route_lib",
        "//source/common/router:metadatamatchcriteria_lib",
        "//source/common/router:retry_policy_lib",
        "//source/extensions/dynamic_modules:abi_context_accessors_lib",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@envoy_api//envoy/extensions/router/cluster_specifiers/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":cluster_specifier_lib",
        "//envoy/common:exception_lib",
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/router/cluster_specifiers/dynamic_modules/v3:pkg_cc_proto",
    ],
)
