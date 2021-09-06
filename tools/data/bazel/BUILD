load("@envoy_repo//:utils.bzl", "py_data")

licenses(["notice"])  # Apache 2

py_data(
    name = "repository_locations",
    filters = ["//filters:interpolate_repository_locations"],
    source = "@envoy//bazel:repository_locations.json",
)

py_data(
    name = "all_repository_locations",
    filters = ["//filters:add_api_repository_locations"],
    source = "@envoy//bazel:repository_locations.json",
)
