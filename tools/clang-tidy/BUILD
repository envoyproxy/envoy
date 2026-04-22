load(":clang_tidy.bzl", "clang_tidy")

licenses(["notice"])  # Apache 2

clang_tidy(
    name = "clang-tidy",
    # TODO: make this work with toolchain_roots
    target = "@llvm_toolchain_llvm//:clang-tidy",
)
