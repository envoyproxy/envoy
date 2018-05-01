load("//bazel:api_build_system.bzl", "api_proto_library", "api_go_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "percent",
    srcs = ["percent.proto"],
    visibility = ["//visibility:public"],
)

api_go_proto_library(
    name = "percent",
    proto = ":percent",
)

api_proto_library(
    name = "range",
    srcs = ["range.proto"],
    visibility = ["//visibility:public"],
)

api_go_proto_library(
    name = "range",
    proto = ":range",
)

api_proto_library(
    name = "string_match",
    srcs = ["string_match.proto"],
    visibility = ["//visibility:public"],
)

api_go_proto_library(
    name = "string_match",
    proto = ":string_match",
)
