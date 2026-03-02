load("@envoy_toolshed//:macros.bzl", "json_data")
load("//bazel:envoy_build_system.bzl", "envoy_cc_test_library", "envoy_contrib_package")
load(":all_contrib_extensions.bzl", "SELECTED_CONTRIB_EXTENSIONS")
load(":contrib_build_config.bzl", "CONTRIB_EXTENSIONS")

licenses(["notice"])  # Apache 2

envoy_contrib_package()

exports_files([
    "extensions_metadata.yaml",
    "contrib_build_config.bzl",
])

json_data(
    name = "contrib_extensions_build_config",
    data = CONTRIB_EXTENSIONS,
)

filegroup(
    name = "config_data",
    srcs = select({
        "//bazel:windows_x86_64": [],
        "//conditions:default": [
            "//contrib/golang/filters/http/test/test_data/dummy:testing_shared_objects",
        ],
    }),
    visibility = ["//visibility:public"],
)

envoy_cc_test_library(
    name = "contrib_test_lib",
    deps = [
        "//test/config_test:config_test_lib",
    ] + SELECTED_CONTRIB_EXTENSIONS,
)
