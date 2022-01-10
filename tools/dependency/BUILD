load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@base_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2

envoy_package()

py_library(
    name = "exports",
    srcs = ["exports.py"],
    data = [
        "//bazel:repository_locations.bzl",
        "@envoy_api//bazel:repository_locations.bzl",
        "@envoy_api//bazel:repository_locations_utils.bzl",
    ],
)

py_library(
    name = "utils",
    srcs = ["utils.py"],
    deps = [":exports"],
)

py_binary(
    name = "cve_scan",
    srcs = ["cve_scan.py"],
    args = ["$(location :cve.yaml)"],
    data = [":cve.yaml"],
    deps = [
        ":utils",
        requirement("envoy.dependency.cve_scan"),
    ],
)

py_binary(
    name = "pip_check",
    srcs = ["pip_check.py"],
    deps = [
        "@envoy_repo",
        requirement("envoy.dependency.pip_check"),
    ],
)
