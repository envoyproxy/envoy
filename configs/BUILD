load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load("@rules_python//python:defs.bzl", "py_binary")
load("@configs_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "configgen",
    srcs = ["configgen.py"],
    data = glob([
        "*.yaml",
    ]),
    deps = [
        requirement("Jinja2"),
        requirement("MarkupSafe"),
    ],
)

filegroup(
    name = "configs",
    srcs = glob(
        [
            "**/*.yaml",
        ],
        exclude = [
            "using_deprecated_config.yaml",
            "**/*.template.yaml",
            "freebind/freebind.yaml",
        ],
    ) + select({
        "//bazel:apple": [],
        "//bazel:windows_x86_64": [],
        "//conditions:default": ["freebind/freebind.yaml"],
    }),
)

genrule(
    name = "example_configs",
    srcs = [
        ":configs",
        "//examples:configs",
        "//docs:configs",
        "//test/config/integration/certs",
    ],
    outs = ["example_configs.tar"],
    cmd = (
        "$(location configgen.sh) $(location configgen) $(@D) " +
        "$(locations :configs) " +
        "$(locations //examples:configs) " +
        "$(locations //docs:configs) " +
        "$(locations //test/config/integration/certs)"
    ),
    tools = [
        "configgen.sh",
        ":configgen",
    ],
)
