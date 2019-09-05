load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v3alpha/route:pkg",
        "//envoy/config/filter/fault/v3alpha:pkg",
        "//envoy/type",
    ],
)

api_proto_library_internal(
    name = "fault",
    srcs = ["fault.proto"],
    deps = [
        "//envoy/api/v3alpha/route",
        "//envoy/config/filter/fault/v3alpha:fault",
        "//envoy/type:percent",
    ],
)
