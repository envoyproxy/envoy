load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@base_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2

envoy_package()

py_library(
    name = "utils",
    srcs = ["utils.py"],
    deps = [
        "//bazel:repository_locations_py",
        "@envoy_api//bazel:repository_locations_py",
    ],
)

py_binary(
    name = "cve_scan",
    srcs = ["cve_scan.py"],
    deps = [":utils"],
)

py_binary(
    name = "ossf_scorecard",
    srcs = ["ossf_scorecard.py"],
    deps = [
        ":utils",
        "//bazel:repository_locations_py",
    ],
)

py_binary(
    name = "cve_scan_test",
    srcs = ["cve_scan_test.py"],
    data = [":cve_scan"],
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
    name = "release_dates",
    srcs = ["release_dates.py"],
    deps = [
        ":utils",
        requirement("colorama"),
        requirement("packaging"),
        requirement("pygithub"),
        "//bazel:repository_locations_py",
    ],
)

py_binary(
    name = "release_dates_api",
    srcs = ["release_dates.py"],
    args = ["$(location @envoy_api//bazel:repository_locations_py)"],
    main = "release_dates.py",
    deps = [
        ":utils",
        requirement("colorama"),
        requirement("packaging"),
        requirement("pygithub"),
        "@envoy_api//bazel:repository_locations_py",
    ],
)

py_binary(
    name = "validate",
    srcs = ["validate.py"],
    deps = [
        ":utils",
        "//source/extensions:extensions_build_config_py",
        "//tools/base:bazel_query",
    ],
)

py_binary(
    name = "validate_test",
    srcs = ["validate_test.py"],
    deps = [":validate"],
)
