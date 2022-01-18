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
