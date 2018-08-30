licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "factory_base_lib",
    hdrs = ["factory_base.h"],
    deps = [
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/filter/http/adaptive_concurrency_limit/v2alpha:adaptive_concurrency_limit_cc",
    ],
)

envoy_cc_library(
    name = "gradient_lib",
    srcs = ["gradient.cc"],
    hdrs = ["gradient.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":factory_base_lib",
        ":well_known_names",
        "//include/envoy/registry",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/upstream:adaptive_concurrency_limit_interface",
        "//source/common/access_log:access_log_formatter_lib",
        "//source/extensions/filters/http/adaptive_concurrency_limit/common:common_lib",
    ],
)

envoy_cc_library(
    name = "well_known_names",
    hdrs = ["well_known_names.h"],
    deps = [
        "//source/common/singleton:const_singleton",
    ],
)
