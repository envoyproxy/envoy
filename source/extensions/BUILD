load("//bazel:utils.bzl", "json_data")
load("//tools/base:envoy_python.bzl", "py_data")
load(":extensions_build_config.bzl", "EXTENSIONS")

licenses(["notice"])  # Apache 2

exports_files([
    "extensions_metadata.yaml",
    "extensions_build_config.bzl",
])

py_data(
    name = "extensions_metadata",
    format = "yaml",
    source = ":extensions_metadata.yaml",
)

py_data(
    name = "extensions_categories",
    filters = ["//tools/extensions:build_categories"],
    format = "yaml",
    source = ":extensions_metadata.yaml",
)

json_data(
    name = "extensions_build_config",
    data = EXTENSIONS,
)

py_data(
    name = "extensions_build_config",
    source = ":extensions_build_config.json",
)
