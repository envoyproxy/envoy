load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("@base_pip3//:requirements.bzl", "entry_point", "requirement")
load("@dev_pip3//:requirements.bzl", dev_requirement = "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_py_binary")

licenses(["notice"])  # Apache 2

envoy_package()

# The upstream libs are maintained here:
#
#    https://github.com/envoyproxy/pytooling/tree/main/envoy.docs.abstract
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

py_library(
    name = "utils",
    srcs = ["utils.py"],
    deps = [
        "//tools/api_proto_plugin",
        "//tools/config_validation:validate_fragment",
        "@envoy_repo//contrib:extensions_categories_py",
        "@envoy_repo//docs:protodoc_manifest_py",
        "@envoy_repo//docs:v2_mapping_py",
        "@envoy_repo//source/extensions:all_extensions_metadata_py",
        "@envoy_repo//source/extensions:extension_status_categories_py",
        "@envoy_repo//source/extensions:extensions_categories_py",
        "@com_google_protobuf//:protobuf_python",
        "@com_github_cncf_udpa//udpa/annotations:pkg_py_proto",
        "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        dev_requirement("envoy.docs.abstract"),
    ],
)

py_binary(
    name = "builder",
    srcs = ["builder.py"],
    deps = [
        ":utils",
        requirement("abstracts"),
        dev_requirement("envoy.docs.abstract"),
        "@envoy_repo//api:v3_proto_rst_py",
        "@envoy_repo//bazel:all_repository_locations_py",
        "@envoy_repo//source/extensions:all_extensions_metadata_py",
        "@envoy_repo//source/extensions:security_postures_py",
        "@envoy_repo//docs:empty_extensions_py",
    ],
)

envoy_py_binary(
    name = "tools.docs.rst_check",
    data = ["//docs:root/version_history/current.rst"],
    deps = [requirement("envoy.base.checker")],
)
