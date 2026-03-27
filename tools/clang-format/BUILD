load(":clang_format.bzl", "clang_format")

licenses(["notice"])  # Apache 2

clang_format(
    name = "clang-format",
    # TODO: make this work with toolchain_roots
    target = "@llvm_toolchain_llvm//:clang-format",
    visibility = ["//visibility:public"],
)
