load("@rules_python//python:defs.bzl", "py_binary")
load("@deps_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "exports",
    srcs = ["exports.py"],
    data = [
        "//bazel:repository_locations.bzl",
        "@envoy_api_canonical//bazel:repository_locations.bzl",
        "@envoy_api_canonical//bazel:repository_locations_utils.bzl",
    ],
)

py_binary(
    name = "generate_external_dep_rst",
    srcs = [
        "generate_external_dep_rst.py",
        "utils.py",
    ],
    data = [
        ":exports",
    ],
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
    visibility = ["//visibility:public"],
    deps = [
        "//tools/base:checker",
        requirement("PyYaml"),
    ],
)

py_binary(
    name = "pytest_pip_check",
    srcs = [
        "pytest_pip_check.py",
        "tests/test_pip_check.py",
    ],
    deps = [
        ":pip_check",
        "//tools/testing:python_pytest",
    ],
)
