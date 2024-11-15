licenses(["notice"])  # Apache 2

exports_files([
    "sections.yaml",
])

filegroup(
    name = "changelogs",
    srcs = glob(["*.*.*.yaml"]) + ["current.yaml"],
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
