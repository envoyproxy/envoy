licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

envoy_package()

envoy_cc_mock(
    name = "safe_init_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/safe_init:manager_interface",
        "//source/common/safe_init:target_lib",
        "//source/common/safe_init:watcher_lib",
    ],
)
