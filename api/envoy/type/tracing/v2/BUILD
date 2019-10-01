load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    name = "tracing",
    deps = ["//envoy/type/matcher"],
)

api_proto_library_internal(
    name = "custom_tag",
    srcs = ["custom_tag.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/type/matcher:metadata",
    ],
)
