load("@aspect_bazel_lib//lib:jq.bzl", "jq")
load("@aspect_bazel_lib//lib:yq.bzl", "yq")
load("@envoy_repo//:path.bzl", "PATH")
load("@rules_shell//shell:sh_binary.bzl", "sh_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

exports_files(glob(["templates/*.html"]))

genrule(
    name = "grcov",
    srcs = ["@grcov"],
    outs = ["grcov_bin"],
    cmd = "cp $< $@ && chmod +x $@",
    executable = True,
    visibility = ["//visibility:public"],
)

jq(
    name = "grcov_config",
    srcs = [],
    out = "grcov_config.json",
    args = [
        "-sR",
        "--arg",
        "macros",
        "$(location :templates/macros.html)",
        "--arg",
        "base",
        "$(location :templates/base.html)",
        "--arg",
        "index",
        "$(location :templates/index.html)",
    ],
    data = [
        ":templates/base.html",
        ":templates/index.html",
        ":templates/macros.html",
    ],
    expand_args = True,
    filter = """
    {templates: {
       "base.html": $base,
       "index.html": $index,
       "macros.html": $macros}}
    """,
)

# This is a bit of a hack, and totally non-hermetic
# grcov needs access to the source files, so the path
# is injected here. This means CoverageReportGenerator cannot
# run remote.
genrule(
    name = "coverage_report_generator_script",
    srcs = ["coverage_report_generator.sh.template"],
    outs = ["coverage_report_generator.sh"],
    cmd = "sed 's|@@WORKSPACE_PATH@@|$$(realpath %s)|g' $< > $@" % PATH,
    tools = ["//tools/coverage:grcov_bin"],
)

sh_binary(
    name = "coverage_report_generator",
    srcs = [":coverage_report_generator_script"],
    data = [
        ":grcov_bin",
        ":grcov_config",
        ":markdown_to_json.jq",
        ":templates/base.html",
        ":templates/index.html",
        ":templates/macros.html",
        "//tools/zstd",
        "@jq_toolchains//:resolved_toolchain",
    ],
    toolchains = ["@jq_toolchains//:resolved_toolchain"],
    visibility = ["//visibility:public"],
)

yq(
    name = "coverage_config",
    srcs = ["//test:per_directory_coverage.yaml"],
    outs = ["coverage_config.json"],
    args = ["-o=json"],
)

sh_binary(
    name = "validate",
    srcs = [":validate.sh"],
    data = [
        ":coverage_config",
        "@jq_toolchains//:resolved_toolchain",
    ],
    env = {
        "JQ_BIN": "$(JQ_BIN)",
        "COVERAGE_CONFIG": "$(location :coverage_config)",
    },
    toolchains = ["@jq_toolchains//:resolved_toolchain"],
    visibility = ["//visibility:public"],
)
