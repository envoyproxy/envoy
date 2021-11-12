load("//bazel:utils.bzl", "json_data")
load(":extensions_build_config.bzl", "EXTENSIONS")

licenses(["notice"])  # Apache 2

exports_files([
    "extensions_metadata.yaml",
    "extensions_build_config.bzl",
])

json_data(
    name = "extensions_build_config",
    data = EXTENSIONS,
)
