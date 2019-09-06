load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

package_group(
    name = "friends",
    includes = [
        "//envoy/api/v3alpha:friends",
    ],
    packages = [
        "//envoy/api/v3alpha/auth",
        "//envoy/api/v3alpha/cluster",
        "//envoy/api/v3alpha/endpoint",
        "//envoy/api/v3alpha/listener",
        "//envoy/api/v3alpha/route",
    ],
)

api_proto_package(
    name = "core",
    deps = [
        "//envoy/type",
    ],
)

api_proto_library_internal(
    name = "address",
    srcs = ["address.proto"],
    visibility = [
        ":friends",
    ],
    deps = [":base"],
)

api_proto_library_internal(
    name = "base",
    srcs = ["base.proto"],
    visibility = [
        ":friends",
    ],
    deps = [
        ":http_uri",
        "//envoy/type:percent",
    ],
)

api_proto_library_internal(
    name = "health_check",
    srcs = ["health_check.proto"],
    visibility = [
        ":friends",
    ],
    deps = [
        ":base",
        "//envoy/type:range",
    ],
)

api_proto_library_internal(
    name = "config_source",
    srcs = ["config_source.proto"],
    visibility = [
        ":friends",
    ],
    deps = [
        ":base",
        ":grpc_service",
    ],
)

api_proto_library_internal(
    name = "http_uri",
    srcs = ["http_uri.proto"],
    visibility = [
        ":friends",
    ],
)

api_proto_library_internal(
    name = "grpc_service",
    srcs = ["grpc_service.proto"],
    visibility = [
        ":friends",
    ],
    deps = [":base"],
)

api_proto_library_internal(
    name = "protocol",
    srcs = ["protocol.proto"],
    visibility = [
        ":friends",
    ],
)
