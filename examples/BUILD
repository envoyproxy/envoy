licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

envoy_package()

filegroup(
    name = "configs",
    srcs = [
        "front-proxy/front-envoy.json",
        "front-proxy/service-envoy.json",
        "grpc-bridge/config/s2s-grpc-envoy.json",
        "grpc-bridge/config/s2s-python-envoy.json",
    ],
)
