load("@rules_python//python:defs.bzl", "py_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@docs_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "generate_extensions_security_rst",
    srcs = ["generate_extensions_security_rst.py"],
    data = ["//source/extensions:extensions_metadata.yaml"],
    deps = [
        requirement("pyyaml"),
    ],
)

genrule(
    name = "extensions_security_rst",
    srcs = ["//source/extensions:extensions_metadata.yaml"],
    outs = ["extensions_security_rst.tar"],
    cmd = (
        "$(location generate_extensions_security_rst) $(location //source/extensions:extensions_metadata.yaml) $@"
    ),
    tools = [":generate_extensions_security_rst"],
)
