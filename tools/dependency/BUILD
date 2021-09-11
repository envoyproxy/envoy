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

py_binary(
    name = "pip_check",
    srcs = ["pip_check.py"],
    deps = [
        "@envoy_repo",
        requirement("envoy.dependency.pip_check"),
    ],
)

py_binary(
    name = "validate",
    srcs = ["validate.py"],
    deps = [
        ":utils",
        "//tools/base:bazel_query",
        "@envoy_repo//api/bazel:repository_locations_py",
        "@envoy_repo//bazel:repository_locations_py",
        "@envoy_repo//source/extensions:extensions_build_config_py",
    ],
)

py_binary(
    name = "validate_test",
    srcs = ["validate_test.py"],
    deps = [":validate"],
)
