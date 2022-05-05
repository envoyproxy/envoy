load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("@base_pip3//:requirements.bzl", "requirement")
load("//tools/base:envoy_python.bzl", "envoy_entry_point")
load("//bazel:envoy_build_system.bzl", "envoy_package")

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
    args = ["$(location //bazel:all_repository_locations)"],
    data = ["//bazel:all_repository_locations"],
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

envoy_entry_point(
    name = "sphinx_runner",
    pkg = "envoy.docs.sphinx_runner",
)

py_binary(
    name = "versions_check",
    srcs = ["versions_check.py"],
    args = [
        "$(location //:VERSION.txt)",
        "$(locations //changelogs)",
    ],
    data = [
        "//:VERSION.txt",
        "//changelogs",
    ],
    deps = [
        ":project",
        requirement("aio.run.checker"),
    ],
)

py_library(
    name = "project",
    srcs = [
        "project.py",
    ],
    deps = [
        requirement("frozendict"),
        requirement("packaging"),
        requirement("pyyaml"),
    ],
)

py_binary(
    name = "generate_version_histories",
    srcs = [
        "generate_version_histories.py",
    ],
    deps = [
        ":project",
        requirement("aio.run.runner"),
        requirement("frozendict"),
        requirement("jinja2"),
        requirement("packaging"),
        requirement("pyyaml"),
    ],
)
