load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

api_proto_library_internal(
    name = "csrf",
    srcs = ["csrf.proto"],
    deps = [
        "//envoy/api/v3alpha/core:base",
        "//envoy/type/matcher:string",
    ],
)
