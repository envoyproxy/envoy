load("@base_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files([
    "google-vrp/envoy-edge.yaml",
])

py_binary(
    name = "configgen",
    srcs = ["configgen.py"],
    data = glob([
        "*.yaml",
    ]),
    deps = [
        requirement("jinja2"),
    ],
)

filegroup(
    name = "files",
    srcs = glob(
        [
            "**/*.yaml",
        ],
        exclude = [
            "using_deprecated_config.yaml",
            "**/*.template.yaml",
            "freebind/freebind.yaml",
            "envoy-tap-config.yaml",
            "envoy-demo.yaml",
        ],
    ) + select({
        "//bazel:apple": ["envoy-demo.yaml"],
        "//bazel:windows_x86_64": [],
        "//bazel:disable_admin_functionality": [],
        "//conditions:default": [
            "envoy-demo.yaml",
            "envoy-tap-config.yaml",
            "freebind/freebind.yaml",
        ],
    }),
)

genrule(
    name = "configs",
    srcs = [
        ":files",
        "//test/config/integration/certs",
    ],
    outs = ["configs.tar"],
    cmd = (
        "$(location configgen.sh) $(location configgen) $@ $(@D) " +
        "$(locations :files) " +
        "$(locations //test/config/integration/certs)"
    ),
    tools = [
        "configgen.sh",
        ":configgen",
    ],
)
