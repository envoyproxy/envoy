load("@base_pip3//:requirements.bzl", "requirement")
load("@envoy_repo//:path.bzl", "PATH")
load("@rules_python//python:defs.bzl", "py_binary")
load(":clang_tidy.bzl", "clang_tidy")

licenses(["notice"])  # Apache 2

clang_tidy(
    name = "clang-tidy",
    # TODO: make this work with toolchain_roots
    target = "@llvm_toolchain_llvm//:clang-tidy",
)

py_binary(
    name = "collect_fixes",
    srcs = ["collect_fixes.py"],
    args = ["--workspace_root=%s" % PATH],
    main = "collect_fixes.py",
    visibility = ["//visibility:public"],
    deps = [requirement("pyyaml")],
)
