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

genrule(
    name = "empty_protos_rst",
    srcs = [
        "//docs:empty_extensions.json",
    ],
    outs = ["empty_protos_rst.tar"],
    cmd = (
        "$(location //tools/protodoc:generate_empty) $(location //docs:empty_extensions.json) $@"
    ),
    tools = ["//tools/protodoc:generate_empty"],
)

py_binary(
    name = "generate_api_rst",
    srcs = [
        "generate_api_rst.py",
    ],
)

genquery(
    name = "v3_proto_srcs",
    expression = "labels(srcs, labels(deps, @envoy_api_canonical//:v3_protos))",
    scope = ["@envoy_api_canonical//:v3_protos"],
)

genrule(
    name = "api_rst",
    srcs = [
        "//tools/protodoc:api_v3_protodoc",
        ":v3_proto_srcs",
    ],
    outs = ["api_rst.tar"],
    cmd = (
        "$(location generate_api_rst) $(location v3_proto_srcs) $(locations //tools/protodoc:api_v3_protodoc) $@"
    ),
    tools = [":generate_api_rst"],
)
