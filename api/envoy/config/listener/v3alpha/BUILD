load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

package_group(
    name = "friends",
    packages = [
        "//envoy/api/v3alpha",
    ],
)

api_proto_package()

api_proto_library_internal(
    name = "api_listener",
    srcs = ["api_listener.proto"],
    visibility = [":friends"],
)
