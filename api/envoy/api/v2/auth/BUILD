load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

package_group(
    name = "friends",
    includes = [
        "//envoy/api/v2:friends",
    ],
    packages = [
        "//envoy/api/v2/cluster",
        "//envoy/api/v2/endpoint",
        "//envoy/api/v2/listener",
        "//envoy/api/v2/route",
    ],
)

api_proto_package(
    name = "auth",
    deps = [
        "//envoy/api/v2/core",
    ],
)

api_proto_library_internal(
    name = "cert",
    srcs = ["cert.proto"],
    visibility = [":friends"],
    deps = [
        "//envoy/api/v2/core:base",
        "//envoy/api/v2/core:config_source",
    ],
)
