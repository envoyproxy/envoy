load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//source/extensions:all_extensions.bzl", "envoy_all_extensions")
load("//tools/base:envoy_python.bzl", "envoy_py_binary")
load("@base_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files([
    "extension_security_postures_metadata.yaml",
])

envoy_py_binary(
    name = "tools.extensions.extensions_check",
    data = [
        "//test/extensions/filters/network/common/fuzz:uber_per_readfilter.cc",
    ] + envoy_all_extensions(),
    deps = [
        requirement("envoy.base.checker"),
        requirement("envoy.base.utils"),
        "//contrib:contrib_extensions_build_config_py",
        "//contrib:extensions_metadata_py",
        "//source/extensions:extensions_build_config_py",
        "//source/extensions:extensions_metadata_py",
    ],
)
