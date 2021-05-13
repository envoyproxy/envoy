load("@rules_python//python:defs.bzl", "py_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "generate_extension_db",
    srcs = ["generate_extension_db.py"],
)

genrule(
    name = "extension_db",
    outs = ["extension_db.json"],
    cmd = "$(location :generate_extension_db) > $@",
    tools = [
        ":generate_extension_db",
        "//source/extensions:extensions_build_config.bzl",
        "//test/extensions/filters/network/common/fuzz:uber_per_readfilter.cc",
    ],
)
