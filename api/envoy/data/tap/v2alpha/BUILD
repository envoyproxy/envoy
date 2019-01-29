load("//bazel:api_build_system.bzl", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

api_proto_library_internal(
    name = "transport",
    srcs = ["transport.proto"],
    deps = ["//envoy/api/v2/core:address"],
)

api_proto_library_internal(
    name = "http",
    srcs = ["http.proto"],
    deps = ["//envoy/api/v2/core:base"],
)

api_proto_library_internal(
    name = "wrapper",
    srcs = ["wrapper.proto"],
    deps = [":http"],
)
