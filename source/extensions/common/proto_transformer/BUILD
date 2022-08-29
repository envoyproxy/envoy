load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "proto_transformer_base_lib",
    hdrs = ["proto_transformer.h"],
    deps = [
        "//source/extensions/filters/http/common:factory_base_lib",
    ],
)
