load("@rules_shell//shell:sh_binary.bzl", "sh_binary")

licenses(["notice"])  # Apache 2

# CI experiment for #47485 only.
sh_binary(
    name = "wrap",
    srcs = ["wrap.sh"],
    visibility = ["//visibility:public"],
)
