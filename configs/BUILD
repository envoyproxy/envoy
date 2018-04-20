licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

envoy_package()

load("//bazel:envoy_build_system.bzl", "envoy_py_test_binary")

envoy_py_test_binary(
    name = "configgen",
    srcs = ["configgen.py"],
    data = glob([
        "*.json",
        "*.yaml",
    ]),
    external_deps = ["jinja2"],
)

filegroup(
    name = "configs",
    srcs = [
        "original-dst-cluster/proxy_config.json",
    ] + select({
        "@bazel_tools//tools/osx:darwin": [],
        "//conditions:default": ["freebind/freebind.yaml"],
    }),
)

genrule(
    name = "example_configs",
    srcs = [
        ":configs",
        "//examples:configs",
        "//test/config/integration/certs",
    ],
    outs = ["example_configs.tar"],
    cmd = "$(location configgen.sh) $(location configgen) $(@D) $(locations :configs) " +
          "$(locations //examples:configs) $(locations //test/config/integration/certs)",
    tools = [
        "configgen.sh",
        ":configgen",
    ],
)
