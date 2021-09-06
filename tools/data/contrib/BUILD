load("@envoy_repo//:utils.bzl", "py_data")

licenses(["notice"])  # Apache 2

py_data(
    name = "extensions_metadata",
    source = "@envoy//contrib:extensions_metadata.yaml",
)

py_data(
    name = "extensions_categories",
    filters = ["//filters:build_categories"],
    source = "@envoy//contrib:extensions_metadata.yaml",
)

py_data(
    name = "extensions_build_config",
    source = "@envoy//contrib:contrib_extensions_build_config.json",
)
