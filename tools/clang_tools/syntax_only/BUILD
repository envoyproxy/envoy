load("//clang_tools/support:clang_tools.bzl", "envoy_clang_tools_cc_binary")

licenses(["notice"])  # Apache 2

envoy_clang_tools_cc_binary(
    name = "syntax_only",
    srcs = ["main.cc"],
    deps = [
        "@clang_tools//:clang_astmatchers",
        "@clang_tools//:clang_basic",
        "@clang_tools//:clang_tooling",
    ],
)
