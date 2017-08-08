licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

envoy_package()

envoy_cc_mock(
    name = "redis_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/redis:command_splitter_interface",
        "//include/envoy/redis:conn_pool_interface",
        "//source/common/common:assert_lib",
        "//source/common/redis:codec_lib",
    ],
)
