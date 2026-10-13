load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "reporter_mocks",
    hdrs = ["reporter.h"],
    deps = [
        "//envoy/extensions/bootstrap/reverse_tunnel:reverse_tunnel_reporter_lib",
    ],
)
