load("//bazel:utils.bzl", "write_json")
load("//tools/base:envoy_python.bzl", "py_data")
load(":contrib_build_config.bzl", "CONTRIB_EXTENSIONS")

licenses(["notice"])  # Apache 2

exports_files([
    "extensions_metadata.yaml",
    "contrib_build_config.bzl",
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

write_json(
    name = "contrib_extensions_build_config",
    data = CONTRIB_EXTENSIONS,
)

py_data(
    name = "contrib_extensions_build_config",
    source = ":contrib_extensions_build_config.json",
)
