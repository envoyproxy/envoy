load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@base_pip3//:requirements.bzl", "requirement")
load("//tools/base:envoy_python.bzl", "envoy_entry_point")
load("@envoy_repo//:path.bzl", "PATH")

licenses(["notice"])  # Apache 2

envoy_package()

py_library(
    name = "utils",
    srcs = ["utils.py"],
)

py_binary(
    name = "cve_scan",
    srcs = ["cve_scan.py"],
    args = [
        "$(location :cve.yaml)",
        "--repository_locations=$(location //bazel:all_repository_locations)",
    ],
    data = [
        ":cve.yaml",
        "//bazel:all_repository_locations",
    ],
    deps = [requirement("envoy.dependency.cve_scan")],
)

envoy_entry_point(
    name = "pip_check",
    args = ["--path=%s" % PATH],
    pkg = "envoy.dependency.pip_check",
)

py_binary(
    name = "validate",
    srcs = ["validate.py"],
    args = [
        "$(location //bazel:all_repository_locations)",
        "$(location //source/extensions:extensions_build_config)",
    ],
    data = [
        "//bazel:all_repository_locations",
        "//source/extensions:extensions_build_config",
    ],
    deps = [
        "//tools/base:bazel_query",
    ],
)

py_binary(
    name = "validate_test",
    srcs = ["validate_test.py"],
    deps = [":validate"],
)
