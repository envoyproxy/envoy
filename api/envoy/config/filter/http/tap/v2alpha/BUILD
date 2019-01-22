load("//bazel:api_build_system.bzl", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

api_proto_library_internal(
    name = "tap",
    srcs = ["tap.proto"],
    deps = [
        "//envoy/service/tap/v2alpha:common",
    ],
)
