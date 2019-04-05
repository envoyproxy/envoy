load("@envoy_api//bazel:api_build_system.bzl", "api_proto_package")

api_proto_package(
    has_services = True,
    deps = [
        "//envoy/service/ratelimit/v2:pkg",
        "@com_github_cncf_udpa//udpa/annotations:pkg",
    ],
)
