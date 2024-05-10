load("@envoy_repo//:path.bzl", "PATH")

licenses(["notice"])  # Apache 2

exports_files([
    "PATCH_README_TPL.md.tmpl",
])

sh_binary(
    name = "patches",
    srcs = ["patches.sh"],
    args = [PATH],
    data = [":PATCH_README.md.tmpl"],
    env = {
        "PATCH_README_TPL": "$(location :PATCH_README.md.tmpl)",
    },
)
