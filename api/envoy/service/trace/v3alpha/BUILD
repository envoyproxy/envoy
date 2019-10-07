load("@envoy_api//bazel:api_build_system.bzl", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    has_services = True,
    deps = [
        "//envoy/api/v3alpha/core:pkg",
        "@opencensus_proto//opencensus/proto/trace/v1:trace_proto",
    ],
)
