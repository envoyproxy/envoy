licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "json_util",
    hdrs = ["json_util.h"],
    external_deps = ["protobuf"],
    deps = [
        ":status",
        "//source/common/protobuf:message",
        "//source/common/protobuf:string",
    ],
)

envoy_cc_library(
    name = "status",
    hdrs = ["status.h"],
    external_deps = ["protobuf"],
)

envoy_cc_library(
    name = "time_util",
    hdrs = ["time_util.h"],
    external_deps = ["protobuf"],
)
