load("@rules_python//python:defs.bzl", "py_binary")
load("@base_pip3//:requirements.bzl", "requirement")
load("//bazel:envoy_build_system.bzl", "envoy_package", "envoy_proto_library")
load("//tools/protodoc:protodoc.bzl", "protodoc_rule")
load("//tools/base:envoy_python.bzl", "envoy_jinja_env", "envoy_py_data")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "generate_empty",
    srcs = ["generate_empty.py"],
    visibility = ["//visibility:public"],
    deps = [":protodoc"],
)

envoy_proto_library(
    name = "manifest_proto",
    srcs = ["manifest.proto"],
)

envoy_py_data(
    name = "extensions_db",
    src = "//source/extensions:extensions_metadata.yaml",
)

envoy_py_data(
    name = "contrib_extensions_db",
    src = "//contrib:extensions_metadata.yaml",
)

envoy_py_data(
    name = "protodoc_manifest_untyped",
    src = "//docs:protodoc_manifest.yaml",
)

py_binary(
    name = "manifest_to_json",
    srcs = ["manifest_to_json.py"],
    deps = [
        ":manifest_proto_py_proto",
        ":protodoc_manifest_untyped",
        "//tools/config_validation:validate_fragment",
        "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        "@com_github_cncf_udpa//udpa/annotations:pkg_py_proto",
        "@com_github_cncf_udpa//xds/annotations/v3:pkg_py_proto",
        "@com_google_protobuf//:protobuf_python",
        requirement("envoy.base.utils"),
    ],
)

genrule(
    name = "manifest",
    outs = ["manifest.json"],
    cmd = """$(location :manifest_to_json) $@""",
    tools = [":manifest_to_json"],
)

envoy_py_data(
    name = "manifest_db",
    src = ":manifest.json",
)

py_binary(
    name = "protodoc",
    srcs = ["protodoc.py"],
    visibility = ["//visibility:public"],
    deps = [
        ":contrib_extensions_db",
        ":extensions_db",
        ":jinja",
        ":manifest_db",
        "//tools/api_proto_plugin",
        "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        "@com_github_cncf_udpa//udpa/annotations:pkg_py_proto",
        "@com_github_cncf_udpa//xds/annotations/v3:pkg_py_proto",
        "@com_google_protobuf//:protobuf_python",
        requirement("envoy.code.check"),
        requirement("Jinja2"),
    ],
)

protodoc_rule(
    name = "api_v3_protodoc",
    deps = ["@envoy_api//:v3_protos"],
)

protodoc_rule(
    name = "xds_protodoc",
    deps = ["@envoy_api//:xds_protos"],
)

py_library(
    name = "rst_filters",
    srcs = ["rst_filters.py"],
)

envoy_jinja_env(
    name = "jinja",
    filters = {
        "rst_anchor": "tools.protodoc.rst_filters.rst_anchor",
        "rst_header": "tools.protodoc.rst_filters.rst_header",
    },
    templates = [
        "templates/empty.rst.tpl",
        "templates/extension.rst.tpl",
        "templates/extension_category.rst.tpl",
    ],
    deps = [
        ":rst_filters",
    ],
)
