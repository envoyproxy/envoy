load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

package_group(
    name = "friends",
    includes = [
        "//envoy/api/v3alpha:friends",
    ],
    packages = [
        "//envoy/api/v3alpha/cluster",
        "//envoy/api/v3alpha/endpoint",
        "//envoy/api/v3alpha/listener",
        "//envoy/api/v3alpha/route",
    ],
)

api_proto_package(
    name = "auth",
    deps = [
        "//envoy/api/v3alpha/core",
    ],
)

api_proto_library_internal(
    name = "cert",
    srcs = ["cert.proto"],
    visibility = [":friends"],
    deps = [
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/core:config_source",
    ],
)
