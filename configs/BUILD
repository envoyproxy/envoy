package(default_visibility = ["//visibility:public"])

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
