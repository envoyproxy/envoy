load("@rules_python//python:defs.bzl", "py_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@base_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2

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
        requirement("twitter.common.rpc"),
        requirement("six"),
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
        requirement("twitter.common.rpc"),
        requirement("six"),
    ],
)
