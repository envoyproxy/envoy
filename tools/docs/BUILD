load("@base_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_entry_point", "envoy_pytool_binary")
load("//tools/python:namespace.bzl", "envoy_py_namespace")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_namespace()

envoy_pytool_binary(
    name = "generate_extensions_security_rst",
    srcs = ["generate_extensions_security_rst.py"],
    deps = [
        requirement("envoy.base.utils"),
    ],
)

envoy_pytool_binary(
    name = "generate_external_deps_rst",
    srcs = ["generate_external_deps_rst.py"],
    args = ["$(location //bazel:all_repository_locations)"],
    data = ["//bazel:all_repository_locations"],
)

envoy_pytool_binary(
    name = "generate_api_rst",
    srcs = ["generate_api_rst.py"],
)

# The upstream lib is maintained here:
#
#    https://github.com/envoyproxy/toolshed/tree/main/envoy.docs.sphinx_runner
#
# Please submit issues/PRs to the toolshed repo:
#
#    https://github.com/envoyproxy/toolshed

envoy_entry_point(
    name = "sphinx_runner",
    pkg = "envoy.docs.sphinx_runner",
    visibility = ["//visibility:public"],
)

envoy_pytool_binary(
    name = "generate_version_histories",
    srcs = ["generate_version_histories.py"],
    deps = [
        requirement("aio.run.runner"),
        requirement("envoy.base.utils"),
        requirement("frozendict"),
        requirement("jinja2"),
        requirement("packaging"),
        requirement("pyyaml"),
    ],
)
