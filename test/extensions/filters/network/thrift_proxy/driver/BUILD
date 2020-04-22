licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_package")

envoy_package()

filegroup(
    name = "generate_fixture",
    srcs = ["generate_fixture.sh"],
    data = [
        ":client",
        ":server",
    ],
)

py_binary(
    name = "client",
    srcs = ["client.py"],
    python_version = "PY2",
    deps = [
        "//test/extensions/filters/network/thrift_proxy/driver/fbthrift:fbthrift_lib",
        "//test/extensions/filters/network/thrift_proxy/driver/finagle:finagle_lib",
        "//test/extensions/filters/network/thrift_proxy/driver/generated/example:example_lib",
        "@com_github_twitter_common_rpc//:twitter_common_rpc",
    ],
)

py_binary(
    name = "server",
    srcs = ["server.py"],
    python_version = "PY2",
    deps = [
        "//test/extensions/filters/network/thrift_proxy/driver/fbthrift:fbthrift_lib",
        "//test/extensions/filters/network/thrift_proxy/driver/finagle:finagle_lib",
        "//test/extensions/filters/network/thrift_proxy/driver/generated/example:example_lib",
        "@com_github_twitter_common_rpc//:twitter_common_rpc",
    ],
)
