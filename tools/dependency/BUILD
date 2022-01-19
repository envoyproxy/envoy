load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@base_pip3//:requirements.bzl", "requirement")

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

py_binary(
    name = "pip_check",
    srcs = ["pip_check.py"],
    deps = [
        "@envoy_repo",
        requirement("envoy.dependency.pip_check"),
    ],
)
