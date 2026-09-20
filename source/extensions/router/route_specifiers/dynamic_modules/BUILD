load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Dynamic modules route specifier.

envoy_extension_package()

envoy_cc_library(
    name = "route_specifier_lib",
    srcs = [
        "abi_impl.cc",
        "route_specifier.cc",
    ],
    hdrs = ["route_specifier.h"],
    deps = [
        "//envoy/common:exception_lib",
        "//envoy/formatter:http_formatter_context_interface",
        "//envoy/http:codes_interface",
        "//envoy/ratelimit:ratelimit_interface",
        "//envoy/router:route_specifier_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/stats:stats_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:matchers_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:statusor_lib",
        "//source/common/common:thread_lib",
        "//source/common/config:metadata_lib",
        "//source/common/config:well_known_names",
        "//source/common/http:hash_policy_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:config_lib",
        "//source/common/router:delegating_route_lib",
        "//source/common/router:metadatamatchcriteria_lib",
        "//source/common/router:retry_policy_lib",
        "//source/common/router:router_ratelimit_lib",
        "//source/common/stats:utility_lib",
        "//source/extensions/dynamic_modules:abi_context_accessors_lib",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/dynamic_modules:metric_registry",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/router/route_specifiers/dynamic_modules/v3:pkg_cc_proto",
    ],
    # Force the ABI callback definitions in abi_impl.cc to be linked and exported so a module that
    # calls them resolves the real implementations rather than the fail closed weak stubs, matching
    # the other dynamic module abi_impl targets.
    alwayslink = True,
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":route_specifier_lib",
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/router/route_specifiers/dynamic_modules/v3:pkg_cc_proto",
    ],
)
