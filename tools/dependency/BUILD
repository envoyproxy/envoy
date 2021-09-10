load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_py_binary")
load("@base_pip3//:requirements.bzl", base_requirement = "requirement")
load("@github_pip3//:requirements.bzl", github_requirement = "requirement")

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
)

py_binary(
    name = "cve_scan",
    srcs = ["cve_scan.py"],
    data = ["@envoy_api//bazel:repository_locations.json"],
    args = ["$(location @envoy_api//bazel:repository_locations.json)"],
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

py_library(
    name = "interpolate_repository_locations",
    srcs = ["interpolate_repository_locations.py"],
)

py_binary(
    name = "release_dates",
    srcs = ["release_dates.py"],
    deps = [
        ":utils",
        base_requirement("colorama"),
        base_requirement("packaging"),
        github_requirement("pygithub"),
    ],
    data = ["//bazel:repository_locations.json"],
    args = ["$(location //bazel:repository_locations.json)"],
)

py_binary(
    name = "release_dates_api",
    srcs = ["release_dates.py"],
    main = "release_dates.py",
    deps = [
        ":utils",
        base_requirement("colorama"),
        base_requirement("packaging"),
        github_requirement("pygithub"),
    ],
    data = ["@envoy_api//bazel:repository_locations.json"],
    args = ["$(location @envoy_api//bazel:repository_locations.json)"],
)
