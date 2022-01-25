load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_py_binary")
load("@base_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_binary(
    name = "tools.extensions.extensions_check",
    args = [
        "--build_config=$(location //source/extensions:extensions_build_config)",
        "--contrib_build_config=$(location //contrib:contrib_extensions_build_config)",
        "--core_extensions=$(location //source/extensions:core_extensions)",
    ],
    data = [
        "//contrib:contrib_extensions_build_config",
        "//contrib:extensions_metadata.yaml",
        "//source/extensions:core_extensions",
        "//source/extensions:extensions_build_config",
        "//source/extensions:extensions_metadata.yaml",
        "//test/extensions/filters/network/common/fuzz:uber_per_readfilter.cc",
    ],
    deps = [
        requirement("aio.run.checker"),
        requirement("envoy.base.utils"),
    ],
)
