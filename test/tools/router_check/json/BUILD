licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "tool_config_schemas_lib",
    srcs = ["tool_config_schemas.cc"],
    hdrs = ["tool_config_schemas.h"],
    deps = ["//source/common/common:macros"],
)
