load("//bazel:api_build_system.bzl", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

api_proto_library_internal(
    name = "fault",
    srcs = ["fault.proto"],
    deps = [
        "//envoy/config/filter/fault/v2:fault",
        "//envoy/type:percent",
        "//envoy/type/matcher:header",
    ],
)
