load("@rules_python//python:defs.bzl", "py_binary")
load("@base_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "verify",
    srcs = ["verify.py"],
    data = [],
    deps = [
         requirement("docker-compose"),
    ],
)

genrule(
    name = "verification",
    cmd = """$(location :verify) > $@""",
    outs = ["verification.txt"],
    tools = [
        ":verify",
    ],
)
