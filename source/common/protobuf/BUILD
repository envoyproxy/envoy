licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "protobuf",
    hdrs = ["protobuf.h"],
    external_deps = ["protobuf"],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    external_deps = ["protobuf"],
    deps = [
        ":protobuf",
        "//source/common/common:utility_lib",
    ],
)
