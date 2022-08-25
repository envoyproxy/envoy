load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "proto_json_marshaller_base_lib",
    hdrs = ["proto_json_marshaller.h"],
    deps = [
        "//source/extensions/filters/http/common:factory_base_lib",
    ],
)
