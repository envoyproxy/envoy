package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "rpc_channel_interface",
    hdrs = ["rpc_channel.h"],
    external_deps = ["protobuf"],
    deps = [
        "//include/envoy/common:optional",
        "//include/envoy/http:header_map_interface",
    ],
)
