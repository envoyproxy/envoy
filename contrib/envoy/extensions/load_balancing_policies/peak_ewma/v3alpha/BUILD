load("@envoy_api//bazel:api_build_system.bzl", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    has_services = False,
    deps = [
        "@com_github_cncf_xds//udpa/annotations:pkg",
        "@com_github_cncf_xds//xds/annotations/v3:pkg",
    ],
)
