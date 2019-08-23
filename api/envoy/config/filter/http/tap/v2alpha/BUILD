load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    name = "http_tap",
    deps = [
        "//envoy/config/common/tap/v2alpha:tap_config",
    ],
)

api_proto_library_internal(
    name = "tap",
    srcs = ["tap.proto"],
    deps = [
        "//envoy/config/common/tap/v2alpha:common",
    ],
)
