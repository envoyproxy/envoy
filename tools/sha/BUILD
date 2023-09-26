load("@envoy_repo//:path.bzl", "PATH")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

sh_binary(
    name = "replace",
    srcs = ["replace.sh"],
    args = [PATH],
)
