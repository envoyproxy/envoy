load("@envoy_repo//:path.bzl", "PATH")
load("@rules_shell//shell:sh_binary.bzl", "sh_binary")

licenses(["notice"])  # Apache 2

sh_binary(
    name = "replace",
    srcs = ["replace.sh"],
    args = [PATH],
)
