licenses(["notice"])  # Apache 2

load("@envoy_api//bazel:api_build_system.bzl", "api_go_proto_library", "api_proto_library_internal")

api_proto_library_internal(
    name = "jwt_authn",
    srcs = ["config.proto"],
    deps = [
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/core:http_uri",
        "//envoy/api/v3alpha/route",
    ],
)

api_go_proto_library(
    name = "jwt_authn",
    proto = ":jwt_authn",
    deps = [
        "//envoy/api/v3alpha/core:base_go_proto",
        "//envoy/api/v3alpha/core:http_uri_go_proto",
        "//envoy/api/v3alpha/route:route_go_proto",
    ],
)
