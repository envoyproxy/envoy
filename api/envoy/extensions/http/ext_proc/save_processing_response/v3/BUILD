load("@envoy_api//bazel:api_build_system.bzl", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/config/core/v3:pkg",
        "//envoy/config/route/v3:pkg",
        "//envoy/service/ext_proc/v3:pkg",
        "@com_github_cncf_xds//udpa/annotations:pkg",
        "@com_github_cncf_xds//xds/annotations/v3:pkg",
    ],
)
