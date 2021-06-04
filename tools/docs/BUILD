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

py_binary(
    name = "generate_external_deps_rst",
    srcs = [
        "generate_external_deps_rst.py",
    ],
    deps = [
        "//tools/dependency:exports",
        "//tools/dependency:utils",
    ],
)

genrule(
    name = "external_deps_rst",
    srcs = [
        "//bazel:repository_locations.bzl",
        "@envoy_api_canonical//bazel:repository_locations.bzl",
        "@envoy_api_canonical//bazel:repository_locations_utils.bzl",
    ],
    outs = ["external_deps_rst.tar"],
    cmd = (
        "$(location generate_external_deps_rst) $@"
    ),
    tools = [":generate_external_deps_rst"],
)
