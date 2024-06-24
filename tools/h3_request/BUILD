load("@base_pip3//:requirements.bzl", "requirement")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load("//tools/base:envoy_python.bzl", "envoy_pytool_binary")
load("//tools/python:namespace.bzl", "envoy_py_namespace")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_namespace()

# To use in tests, you probably will want to include
# args="--ca-certs=$(location //test/config/integration/certs:cacert.pem)"
envoy_pytool_binary(
    name = "h3_request",
    srcs = ["h3_request.py"],
    deps = [
        requirement("aioquic"),
    ],
)
