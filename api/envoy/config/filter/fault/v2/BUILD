load("//bazel:api_build_system.bzl", "api_proto_library")

licenses(["notice"])  # Apache 2

api_proto_library(
    name = "fault",
    srcs = ["fault.proto"],
    visibility = [
        "//envoy/config/filter/http/fault/v2:__pkg__",
        "//envoy/config/filter/network/mongo_proxy/v2:__pkg__",
    ],
)
