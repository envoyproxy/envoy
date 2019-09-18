load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    name = "type",
)

api_proto_library_internal(
    name = "http_status",
    srcs = ["http_status.proto"],
    visibility = ["//visibility:public"],
)

api_proto_library_internal(
    name = "percent",
    srcs = ["percent.proto"],
    visibility = ["//visibility:public"],
)

api_proto_library_internal(
    name = "range",
    srcs = ["range.proto"],
    visibility = ["//visibility:public"],
)
