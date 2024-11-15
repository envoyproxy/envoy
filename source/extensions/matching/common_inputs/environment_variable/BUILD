load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "input_lib",
    hdrs = ["input.h"],
    deps = [
        "//envoy/matcher:matcher_interface",
        "//source/common/common:hash_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":input_lib",
        "//envoy/matcher:matcher_interface",
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "@envoy_api//envoy/extensions/matching/common_inputs/environment_variable/v3:pkg_cc_proto",
    ],
)
