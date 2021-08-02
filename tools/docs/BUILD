load("@rules_python//python:defs.bzl", "py_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("@docs_pip3//:requirements.bzl", "requirement")
load("//tools/base:envoy_python.bzl", "envoy_py_binary")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "generate_extensions_security_rst",
    srcs = ["generate_extensions_security_rst.py"],
    data = ["//source/extensions:extensions_metadata.yaml"],
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
        requirement("alabaster"),
        requirement("Babel"),
        requirement("certifi"),
        requirement("chardet"),
        requirement("colorama"),
        requirement("docutils"),
        requirement("gitdb"),
        requirement("GitPython"),
        requirement("idna"),
        requirement("imagesize"),
        requirement("Jinja2"),
        requirement("MarkupSafe"),
        requirement("packaging"),
        requirement("Pygments"),
        requirement("pyparsing"),
        requirement("pytz"),
        requirement("requests"),
        requirement("setuptools"),
        requirement("six"),
        requirement("smmap"),
        requirement("snowballstemmer"),
        requirement("Sphinx"),
        requirement("sphinx-copybutton"),
        requirement("sphinx-rtd-theme"),
        requirement("sphinx-tabs"),
        requirement("sphinxcontrib-applehelp"),
        requirement("sphinxcontrib-devhelp"),
        requirement("sphinxcontrib-htmlhelp"),
        requirement("sphinxcontrib-httpdomain"),
        requirement("sphinxcontrib-jsmath"),
        requirement("sphinxcontrib-qthelp"),
        requirement("sphinxext-rediraffe"),
        requirement("sphinxcontrib-serializinghtml"),
        requirement("urllib3"),
    ],
)

envoy_py_binary(
    name = "tools.docs.rst_check",
    data = ["//docs:root/version_history/current.rst"],
    deps = ["//tools/base:checker"],
)
