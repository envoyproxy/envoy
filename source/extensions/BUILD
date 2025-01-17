load("@envoy_toolshed//:macros.bzl", "json_data")
load("//bazel:envoy_build_system.bzl", "envoy_extension_package")
load(":all_extensions.bzl", "envoy_all_extensions")
load(":extensions_build_config.bzl", "EXTENSIONS")

licenses(["notice"])  # Apache 2

envoy_extension_package()

exports_files([
    "extensions_metadata.yaml",
    "extensions_build_config.bzl",
])

json_data(
    name = "extensions_build_config",
    data = EXTENSIONS,
)

json_data(
    name = "core_extensions",
    data = envoy_all_extensions(),
)
