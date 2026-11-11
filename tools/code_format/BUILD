load("@base_pip3//:requirements.bzl", "requirement")
load("@envoy_repo//:path.bzl", "PATH")
load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")

licenses(["notice"])  # Apache 2

py_library(
    name = "envoy_build_fixer",
    srcs = ["envoy_build_fixer.py"],
)

py_binary(
    name = "check_format",
    srcs = ["check_format.py"],
    args = [
        "--path=%s" % PATH,
        "--clang_format_path=$(location //tools/clang-format)",
        "--buildifier_path=$(location //bazel/deps:buildifier)",
        "--buildozer_path=$(location //bazel/deps:buildozer)",
    ],
    data = [
        ":config.yaml",
        "//bazel/deps:buildifier",
        "//bazel/deps:buildozer",
        "//tools/clang-format",
    ],
    deps = [
        requirement("pyyaml"),
        ":envoy_build_fixer",
    ],
)

py_test(
    name = "check_format_test",
    srcs = [
        "check_format.py",
        "check_format_test.py",
    ],
    data = [":config.yaml"],
    deps = [
        requirement("pyyaml"),
        ":envoy_build_fixer",
    ],
)
