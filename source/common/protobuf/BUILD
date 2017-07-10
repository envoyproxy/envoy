licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "descriptor",
    hdrs = ["descriptor.h"],
    external_deps = ["protobuf"],
)

envoy_cc_library(
    name = "message",
    hdrs = ["message.h"],
    external_deps = ["protobuf"],
)

envoy_cc_library(
    name = "repeated_field",
    hdrs = ["repeated_field.h"],
    external_deps = ["protobuf"],
)

envoy_cc_library(
    name = "string",
    hdrs = ["string.h"],
)

envoy_cc_library(
    name = "types",
    hdrs = ["types.h"],
)
