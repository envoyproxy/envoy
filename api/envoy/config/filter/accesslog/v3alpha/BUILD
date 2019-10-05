load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v3alpha/core",
        "//envoy/api/v3alpha/route:pkg",
        "//envoy/type",
    ],
)

api_proto_library_internal(
    name = "accesslog",
    srcs = ["accesslog.proto"],
    visibility = [
        "//envoy/config/filter/http/router/v3alpha:__pkg__",
        "//envoy/config/filter/network/http_connection_manager/v3alpha:__pkg__",
        "//envoy/config/filter/network/tcp_proxy/v3alpha:__pkg__",
    ],
    deps = [
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/route",
        "//envoy/type:percent",
    ],
)
