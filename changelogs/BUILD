licenses(["notice"])  # Apache 2

exports_files([
    "changelogs.yaml",
])

filegroup(
    name = "changelogs",
    srcs = glob([
        "*.*.*.yaml",
        "current/**/*.rst",
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
