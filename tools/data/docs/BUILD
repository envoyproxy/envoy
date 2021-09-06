load("@envoy_repo//:utils.bzl", "py_data")

licenses(["notice"])  # Apache 2

py_data(
    name = "protodoc_manifest",
    cache = False,
    filters = ["//filters:protodoc_protobuf_manifest"],
    source = "@envoy//docs:protodoc_manifest.yaml",
)

py_data(
    name = "v2_mapping",
    source = "@envoy//docs:v2_mapping.json",
)

py_data(
    name = "empty_extensions",
    filters = ["//filters:empty_extensions"],
    source = "@envoy//docs:empty_extensions.json",
)
