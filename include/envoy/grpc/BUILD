licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "rpc_channel_interface",
    hdrs = ["rpc_channel.h"],
    external_deps = ["protobuf"],
    deps = [
        "//include/envoy/common:optional",
        "//include/envoy/http:header_map_interface",
    ],
)

envoy_cc_library(
    name = "status_interface",
    hdrs = ["status.h"],
)

