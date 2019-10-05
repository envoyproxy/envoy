load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v3alpha/core",
        "//envoy/type",
        "//envoy/type/matcher",
    ],
)

api_proto_library_internal(
    name = "ext_authz",
    srcs = ["ext_authz.proto"],
    deps = [
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/core:grpc_service",
        "//envoy/api/v3alpha/core:http_uri",
        "//envoy/type:http_status",
        "//envoy/type/matcher:string",
    ],
)
