load("@envoy_repo//:path.bzl", "PATH")

licenses(["notice"])  # Apache 2

sh_binary(
    name = "upload",
    srcs = ["upload.sh"],
    data = ["//tools/gsutil"],
    env = {
        "ENVOY_SOURCE_DIR": PATH,
        "GSUTIL": "$(location //tools/gsutil)",
    },
)
