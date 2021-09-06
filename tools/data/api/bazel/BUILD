load("@envoy_repo//:utils.bzl", "py_data")

licenses(["notice"])  # Apache 2

py_data(
    name = "repository_locations",
    filters = ["//filters:interpolate_repository_locations"],
    source = "@envoy_api//bazel:repository_locations.json",
)
