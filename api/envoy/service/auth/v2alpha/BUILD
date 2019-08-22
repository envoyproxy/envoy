load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    name = "external_auth_v2alpha",
    srcs = glob(["*.proto"]),
    has_services = True,
    deps = [
        "//envoy/service/auth/v2:external_auth_service",
    ],
)

api_proto_library_internal(
    name = "external_auth",
    srcs = [
        "external_auth.proto",
    ],
    has_services = 1,
    deps = ["//envoy/service/auth/v2:external_auth"],
)
