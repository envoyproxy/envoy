load("@rules_python//python:defs.bzl", "py_binary")
load("@base_pip3//:requirements.bzl", "entry_point", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_py_binary")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "generate_extensions_security_rst",
    srcs = ["generate_extensions_security_rst.py"],
    deps = [
        requirement("envoy.base.utils"),
    ],
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

py_binary(
    name = "generate_api_rst",
    srcs = [
        "generate_api_rst.py",
    ],
)

# The upstream lib is maintained here:
#
#    https://github.com/envoyproxy/pytooling/tree/main/envoy.docs.sphinx_runner
#
# Please submit issues/PRs to the pytooling repo:
#
#    https://github.com/envoyproxy/pytooling

alias(
    name = "sphinx_runner",
    actual = entry_point(
        pkg = "envoy.docs.sphinx_runner",
        script = "envoy.docs.sphinx_runner",
    ),
)

envoy_py_binary(
    name = "tools.docs.rst_check",
    data = ["//docs:root/version_history/current.rst"],
    deps = [requirement("envoy.base.checker")],
)
