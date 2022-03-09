load("@rules_python//python:defs.bzl", "py_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_entry_point")
load("@base_pip3//:requirements.bzl", "requirement")
load("@envoy_repo//:path.bzl", "PATH")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_entry_point(
    name = "check",
    args = [
        "--repository_locations=$(location //bazel:all_repository_locations)",
        "--cve_config=$(location :cve.yaml)",
    ],
    data = [
        ":cve.yaml",
        "//bazel:all_repository_locations",
    ],
    pkg = "envoy.dependency.check",
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
        "@envoy_repo",
        requirement("aio.api.bazel"),
    ],
)

py_binary(
    name = "validate_test",
    srcs = ["validate_test.py"],
    deps = [":validate"],
)
