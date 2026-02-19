load("@docs_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_binary")
load("@rules_python//python/entry_points:py_console_script_binary.bzl", "py_console_script_binary")

licenses(["notice"])  # Apache 2

py_binary(
    name = "generate_extensions_security_rst",
    srcs = ["generate_extensions_security_rst.py"],
    visibility = ["//visibility:public"],
    deps = [
        requirement("envoy.base.utils"),
    ],
)

py_binary(
    name = "generate_external_deps_rst",
    srcs = ["generate_external_deps_rst.py"],
    args = ["$(location @envoy//bazel:all_repository_locations)"],
    data = ["@envoy//bazel:all_repository_locations"],
    visibility = ["//visibility:public"],
)

py_binary(
    name = "generate_api_rst",
    srcs = ["generate_api_rst.py"],
    visibility = ["//visibility:public"],
)

# The upstream lib is maintained here:
#
#    https://github.com/envoyproxy/toolshed/tree/main/envoy.docs.sphinx_runner
#
# Please submit issues/PRs to the toolshed repo:
#
#    https://github.com/envoyproxy/toolshed

py_console_script_binary(
    name = "sphinx_runner",
    pkg = "@docs_pip3//envoy_docs_sphinx_runner",
    script = "envoy.docs.sphinx_runner",
    visibility = ["//visibility:public"],
)

py_binary(
    name = "generate_version_histories",
    srcs = ["generate_version_histories.py"],
    visibility = ["//visibility:public"],
    deps = [
        requirement("aio.run.runner"),
        requirement("envoy.base.utils"),
        requirement("frozendict"),
        requirement("jinja2"),
        requirement("packaging"),
        requirement("pyyaml"),
    ],
)
