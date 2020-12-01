load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

filegroup(
    name = "test_with_kill_request_filter",
    srcs = [
        "test_with_kill_request_filter.yaml",
    ],
)
