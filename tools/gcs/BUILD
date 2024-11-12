load("@envoy_repo//:path.bzl", "PATH")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

sh_binary(
    name = "upload",
    srcs = ["upload.sh"],
    data = ["//tools/gsutil"],
    env = {
        "ENVOY_SOURCE_DIR": PATH,
        "GSUTIL": "$(location //tools/gsutil)",
    },
)
