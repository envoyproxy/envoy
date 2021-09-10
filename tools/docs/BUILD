load("@rules_python//python:defs.bzl", "py_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@base_pip3//:requirements.bzl", "requirement")
load("//tools/base:envoy_python.bzl", "envoy_py_binary")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "generate_extensions_security_rst",
    srcs = ["generate_extensions_security_rst.py"],
    deps = [
        "//tools/base:utils",
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

envoy_py_binary(
    name = "tools.docs.sphinx_runner",
    deps = [
        "//tools/base:runner",
        "//tools/base:utils",
        requirement("colorama"),
        requirement("Sphinx"),
        requirement("sphinx-copybutton"),
        requirement("sphinx-rtd-theme"),
        requirement("sphinx-tabs"),
        requirement("sphinxcontrib-httpdomain"),
        requirement("sphinxcontrib-serializinghtml"),
        requirement("sphinxext-rediraffe"),
    ],
)

envoy_py_binary(
    name = "tools.docs.rst_check",
    data = ["//docs:root/version_history/current.rst"],
    deps = ["//tools/base:checker"],
)
