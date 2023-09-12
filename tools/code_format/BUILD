load("@base_pip3//:requirements.bzl", "requirement")
load("@envoy_repo//:path.bzl", "PATH")
load("@rules_python//python:defs.bzl", "py_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

py_library(
    name = "envoy_build_fixer",
    srcs = ["envoy_build_fixer.py"],
)

py_library(
    name = "header_order",
    srcs = ["header_order.py"],
)

py_binary(
    name = "check_format",
    srcs = ["check_format.py"],
    args = [
        "--path=%s" % PATH,
        "--clang_format_path=$(location //tools/clang-format)",
        "--buildifier_path=$(location @com_github_bazelbuild_buildtools//buildifier)",
        "--buildozer_path=$(location @com_github_bazelbuild_buildtools//buildozer)",
    ],
    data = [
        ":config.yaml",
        "//tools/clang-format",
        "@com_github_bazelbuild_buildtools//buildifier",
        "@com_github_bazelbuild_buildtools//buildozer",
    ],
    deps = [
        requirement("pyyaml"),
        ":envoy_build_fixer",
        ":header_order",
    ],
)
