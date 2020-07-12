load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_py_test_binary",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_py_test_binary(
    name = "configgen",
    srcs = ["configgen.py"],
    data = glob([
        "*.yaml",
    ]),
    external_deps = ["jinja2"],
)

filegroup(
    name = "configs",
    srcs = [
        "google-vrp/envoy-edge.yaml",
        "google-vrp/envoy-origin.yaml",
        "original-dst-cluster/proxy_config.yaml",
    ] + select({
        "//bazel:apple": [],
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
