load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_py_binary")

licenses(["notice"])  # Apache 2

envoy_package()

py_library(
    name = "exports",
    srcs = ["exports.py"],
    data = [
        "//bazel:repository_locations.bzl",
        "@envoy_api_canonical//bazel:repository_locations.bzl",
        "@envoy_api_canonical//bazel:repository_locations_utils.bzl",
    ],
)

py_library(
    name = "utils",
    srcs = ["utils.py"],
    deps = [":exports"],
)

py_binary(
    name = "cve_scan",
    srcs = [
        "cve_scan.py",
        "utils.py",
    ],
    data = [
        ":exports",
    ],
)

py_binary(
    name = "cve_scan_test",
    srcs = ["cve_scan_test.py"],
    data = [
        ":cve_scan",
    ],
)

envoy_py_binary(
    name = "tools.dependency.pip_check",
    deps = [
        "//tools/base:checker",
        "//tools/base:utils",
    ],
)
