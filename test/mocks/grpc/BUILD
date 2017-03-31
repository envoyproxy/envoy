package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_mock")

envoy_cc_mock(
    name = "grpc_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = ["//include/envoy/grpc:rpc_channel_includes"],
)
