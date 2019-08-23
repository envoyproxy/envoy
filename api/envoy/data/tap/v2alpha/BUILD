load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = ["//envoy/api/v2/core"],
)

api_proto_library_internal(
    name = "common",
    srcs = ["common.proto"],
)

api_proto_library_internal(
    name = "transport",
    srcs = ["transport.proto"],
    deps = [
        ":common",
        "//envoy/api/v2/core:address",
    ],
)

api_proto_library_internal(
    name = "http",
    srcs = ["http.proto"],
    deps = [
        ":common",
        "//envoy/api/v2/core:base",
    ],
)

api_proto_library_internal(
    name = "wrapper",
    srcs = ["wrapper.proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":http",
        ":transport",
        "//envoy/api/v2/core:address",
    ],
)
