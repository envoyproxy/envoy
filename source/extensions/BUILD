load("@bazel_skylib//rules:common_settings.bzl", "string_flag")
load("@envoy_toolshed//:macros.bzl", "json_data")
load("//bazel:envoy_build_system.bzl", "envoy_extension_package")
load(":all_extensions.bzl", "envoy_all_extensions")
load(":extensions_build_config.bzl", "update_extensions")
load(":extensions_build_config.bzl", "EXTENSIONS")

licenses(["notice"])  # Apache 2

envoy_extension_package()

string_flag(
    name = "additional_extensions",
    build_setting_default = "",
)

update_extensions(
    name = "updated_extensions",
    additional_extensions = ":additional_extensions",
)

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
