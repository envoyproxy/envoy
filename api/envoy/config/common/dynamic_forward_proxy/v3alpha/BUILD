load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

api_proto_library_internal(
    name = "dns_cache",
    srcs = ["dns_cache.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v3alpha:cds",
    ],
)
