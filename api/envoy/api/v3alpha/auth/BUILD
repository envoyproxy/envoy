load("@envoy_api//bazel:api_build_system.bzl", "api_go_proto_library", "api_proto_library_internal")

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

api_proto_library_internal(
    name = "cert",
    srcs = ["cert.proto"],
    visibility = [":friends"],
    deps = [
        "//envoy/api/v3alpha/core:base",
        "//envoy/api/v3alpha/core:config_source",
    ],
)

api_go_proto_library(
    name = "cert",
    proto = ":cert",
    deps = [
        "//envoy/api/v3alpha/core:base_go_proto",
        "//envoy/api/v3alpha/core:config_source_go_proto",
    ],
)
