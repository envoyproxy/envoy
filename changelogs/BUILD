load("//tools/base:yaml.bzl", "yaml_sorted_mapping")

licenses(["notice"])  # Apache 2

exports_files([
    "changelogs.yaml",
])

yaml_sorted_mapping(
    name = "sort_areas",
    src = "changelogs.yaml",
    mapping = "areas",
    visibility = ["//visibility:public"],
)

filegroup(
    name = "changelogs",
    srcs = glob([
        "*.*.*.yaml",
        "current/**/*.rst",
        "current/PLACEHOLDER",
    ]) + [
        "changelogs.yaml",
    ],
    visibility = ["//visibility:public"],
)

genrule(
    name = "summary",
    outs = ["summary.txt"],
    cmd = """
    cat $(location :summary.md) > $@
    """,
    tools = ["summary.md"],
    visibility = ["//visibility:public"],
)
