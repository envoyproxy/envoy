load("@rules_python//python:defs.bzl", "py_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//source/extensions:all_extensions.bzl", "envoy_all_extensions")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "validate_extensions",
    srcs = ["validate_extensions.py"],
    data = [
        "@com_github_bazelbuild_buildtools//buildozer:buildozer",
        "//source/extensions:extensions_metadata.yaml",
        "//source/extensions:extensions_build_config.bzl",
        "//test/extensions/filters/network/common/fuzz:uber_per_readfilter.cc",
    ] + envoy_all_extensions(),
)
