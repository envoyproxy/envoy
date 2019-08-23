load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = ["//envoy/type"],
)

api_proto_library_internal(
    name = "fault",
    srcs = ["fault.proto"],
    visibility = [
        "//envoy/config/filter/http/fault/v2:__pkg__",
        "//envoy/config/filter/network/mongo_proxy/v2:__pkg__",
    ],
    deps = ["//envoy/type:percent"],
)
