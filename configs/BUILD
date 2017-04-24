load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

envoy_package()

genrule(
    name = "example_configs",
    srcs = glob(["*.json"]) + ["//examples:configs"],
    outs = ["example_configs.tar"],
    cmd = "$(location configgen.sh) $(@D) $(locations //examples:configs)",
    tools = [
        "configgen.py",
        "configgen.sh",
        "requirements.txt",
    ],
)
