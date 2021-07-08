load("@rules_python//python:defs.bzl", "py_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@distribution_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "publish",
    srcs = ["publish.py"],
    deps = [
        "//tools/base:runner",
        requirement("aiodocker"),
        requirement("pyyaml"),
    ],
)

py_binary(
    name = "verify",
    srcs = ["verify.py"],
    data = [
        "//builds:distros",
        "//builds:distrotest.sh",
    ],
    deps = [
        "//tools/base:checker",
        requirement("aiodocker"),
        requirement("pyyaml"),
    ],
    args = [
        "$(location //builds:distrotest.sh)",
        "$(location //builds:distros)",
    ],
)
