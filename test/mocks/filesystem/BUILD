load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "filesystem_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//envoy/filesystem:filesystem_interface",
        "//envoy/filesystem:watcher_interface",
        "//source/common/common:thread_lib",
    ],
)
