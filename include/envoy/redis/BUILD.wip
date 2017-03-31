package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "codec_includes",
    hdrs = ["codec.h"],
    deps = ["//include/envoy/buffer:buffer_includes"],
)

envoy_cc_library(
    name = "command_splitter_includes",
    hdrs = ["command_splitter.h"],
    deps = [":codec_includes"],
)

envoy_cc_library(
    name = "conn_pool_includes",
    hdrs = ["conn_pool.h"],
    deps = [
        ":codec_includes",
        "//include/envoy/upstream:cluster_manager_includes",
    ],
)
