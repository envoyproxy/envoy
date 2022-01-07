load("//bazel:utils.bzl", "json_data")
load(":contrib_build_config.bzl", "CONTRIB_EXTENSIONS")

licenses(["notice"])  # Apache 2

exports_files([
    "extensions_metadata.yaml",
    "contrib_build_config.bzl",
])

json_data(
    name = "contrib_extensions_build_config",
    data = CONTRIB_EXTENSIONS,
)
