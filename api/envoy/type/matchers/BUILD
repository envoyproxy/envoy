load("//bazel:api_build_system.bzl", "api_go_proto_library", "api_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "metadata",
    srcs = ["metadata.proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":number",
        ":string",
    ],
)

api_go_proto_library(
    name = "metadata",
    proto = ":metadata",
)

api_proto_library(
    name = "number",
    srcs = ["number.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/type:range",
    ],
)

api_go_proto_library(
    name = "number",
    proto = ":number",
)

api_proto_library(
    name = "string",
    srcs = ["string.proto"],
    visibility = ["//visibility:public"],
)

api_go_proto_library(
    name = "string",
    proto = ":string",
)
