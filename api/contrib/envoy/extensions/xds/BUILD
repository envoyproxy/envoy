load("@envoy_api//bazel:api_build_system.bzl", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/config/common/key_value/v3:pkg",
        "@com_github_cncf_udpa//udpa/annotations:pkg",
        "@envoy_api//envoy/service/discovery/v3:pkg",
    ],
)
