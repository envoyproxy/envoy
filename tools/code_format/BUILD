load("@base_pip3//:requirements.bzl", "requirement")
load("@envoy_repo//:path.bzl", "PATH")
load("@rules_python//python:defs.bzl", "py_binary", "py_library")

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
        "--buildifier_path=$(location @buildifier_prebuilt//:buildifier)",
        "--buildozer_path=$(location @buildifier_prebuilt//:buildozer)",
    ],
    data = [
        ":config.yaml",
        "//tools/clang-format",
        "@buildifier_prebuilt//:buildifier",
        "@buildifier_prebuilt//:buildozer",
    ],
    deps = [
        requirement("pyyaml"),
        ":envoy_build_fixer",
    ],
)
