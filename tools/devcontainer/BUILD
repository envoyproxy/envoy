load("//tools/devcontainer:tools.bzl", "create_tool_links")

licenses(["notice"])  # Apache 2

create_tool_links(
    name = "create_tool_links",
    tools = [
        "@llvm_toolchain_llvm//:bin/clangd",
        "@llvm_toolchain_llvm//:bin/clang-format",
        "@llvm_toolchain//:bin/cc_wrapper.sh",
    ],
)
