load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package()

api_proto_library(
    name = "outlier_detection_event",
    srcs = ["outlier_detection_event.proto"],
    visibility = [
        "//visibility:public",
    ],
)
