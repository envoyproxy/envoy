load("@rules_python//python:defs.bzl", "py_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")

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
    deps = [
        "//test/extensions/filters/network/thrift_proxy/driver/fbthrift:fbthrift_lib",
        "//test/extensions/filters/network/thrift_proxy/driver/generated/example:example_lib",
    ],
)

py_binary(
    name = "server",
    srcs = ["server.py"],
    deps = [
        "//test/extensions/filters/network/thrift_proxy/driver/fbthrift:fbthrift_lib",
        "//test/extensions/filters/network/thrift_proxy/driver/generated/example:example_lib",
    ],
)
