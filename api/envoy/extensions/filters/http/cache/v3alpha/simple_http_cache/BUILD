load("@envoy_api//bazel:api_build_system.bzl", "api_proto_package")

licenses(["notice"])  # Apache 2

## WIP: Simple in-memory cache storage plugin. Not ready for deployment.

api_proto_package(
    deps = [
        "//envoy/config/filter/http/cache/v2alpha:pkg",
        "//envoy/config/route/v3:pkg",
        "//envoy/type/matcher/v3:pkg",
        "@com_github_cncf_udpa//udpa/annotations:pkg",
    ],
)
