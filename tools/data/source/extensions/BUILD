load("@envoy_repo//:utils.bzl", "py_data")

licenses(["notice"])  # Apache 2

py_data(
    name = "extensions_metadata",
    source = "@envoy//source/extensions:extensions_metadata.yaml",
)

py_data(
    name = "extensions_categories",
    filters = ["//filters:build_categories"],
    source = "@envoy//source/extensions:extensions_metadata.yaml",
)

py_data(
    name = "extensions_build_config",
    source = "@envoy//source/extensions:extensions_build_config.json",
)

py_data(
    name = "all_extensions_metadata",
    filters = ["//filters:add_contrib_extensions_metadata"],
    source = "@envoy//source/extensions:extensions_metadata.yaml",
)

py_data(
    name = "security_postures",
    filters = [
        "//filters:add_contrib_extensions_metadata",
        "//filters:extension_security_postures",
    ],
    source = "@envoy//source/extensions:extensions_metadata.yaml",
)

py_data(
    name = "extension_status_categories",
    source = "@envoy//source/extensions:extension_status_categories.yaml",
)
