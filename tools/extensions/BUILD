load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//source/extensions:all_extensions.bzl", "envoy_all_extensions")
load("//tools/base:envoy_python.bzl", "envoy_py_binary")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_binary(
    name = "tools.extensions.extensions_check",
    data = [
        "//source/extensions:extensions_metadata.yaml",
        "//source/extensions:extensions_build_config.bzl",
        "//test/extensions/filters/network/common/fuzz:uber_per_readfilter.cc",
    ] + envoy_all_extensions(),
    deps = [
        "//tools/base:checker",
        "//tools/base:utils",
    ],
)
