load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

package_group(
    name = "friends",
    packages = [
        "//envoy/api/v2",
    ],
)

api_proto_package(
    deps = [
        "//envoy/config/filter/network/http_connection_manager/v2:http_connection_manager",
    ],
)

api_proto_library_internal(
    name = "api_listener",
    srcs = ["api_listener.proto"],
    visibility = [":friends"],
    deps = [
        "//envoy/config/filter/network/http_connection_manager/v2:http_connection_manager",
    ],
)
