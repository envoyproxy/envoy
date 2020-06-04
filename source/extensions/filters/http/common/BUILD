load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "pass_through_filter_lib",
    hdrs = ["pass_through_filter.h"],
    deps = [
        "//include/envoy/server:filter_config_interface",
    ],
)

envoy_cc_library(
    name = "factory_base_lib",
    hdrs = ["factory_base.h"],
    deps = [
        "//include/envoy/server:filter_config_interface",
    ],
)

envoy_cc_library(
    name = "jwks_fetcher_lib",
    srcs = ["jwks_fetcher.cc"],
    hdrs = ["jwks_fetcher.h"],
    external_deps = [
        "jwt_verify_lib",
    ],
    deps = [
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/http:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    deps = [
        "//include/envoy/runtime:runtime_interface",
        "//source/common/common:macros",
        "//source/extensions/common:utility_lib",
        "//source/extensions/filters/http:well_known_names",
    ],
)
