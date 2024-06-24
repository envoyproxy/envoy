load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files([
    "sections.yaml",
])

filegroup(
    name = "changelogs",
    srcs = glob(["*.*.*.yaml"]) + ["current.yaml"],
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
