load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

envoy_package()

envoy_cc_mock(
    name = "grpc_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = ["//include/envoy/grpc:rpc_channel_interface"],
)
