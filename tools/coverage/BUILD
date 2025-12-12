load("@aspect_bazel_lib//lib:jq.bzl", "jq")
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
    name = "report_generator_script",
    srcs = ["report_generator.sh.template"],
    outs = ["report_generator.sh"],
    cmd = "sed 's|@@WORKSPACE_PATH@@|$$(realpath %s)|g' $< > $@" % PATH,
)

label_flag(
    name = "config",
    build_setting_default = "//test:coverage_config",
    visibility = ["//visibility:public"],
)

sh_binary(
    name = "report_generator",
    srcs = [":report_generator_script"],
    data = [
        ":config",
        ":filter.jq",
        ":grcov_bin",
        ":grcov_config",
        ":templates/base.html",
        ":templates/index.html",
        ":templates/macros.html",
        "//tools/zstd",
        "@jq_toolchains//:resolved_toolchain",
    ],
    toolchains = ["@jq_toolchains//:resolved_toolchain"],
    visibility = ["//visibility:public"],
)

sh_binary(
    name = "validate",
    srcs = [":validate.sh"],
    data = [
        ":config",
        "@jq_toolchains//:resolved_toolchain",
    ],
    env = {
        "JQ_BIN": "$(JQ_BIN)",
        "COVERAGE_CONFIG": "$(location :config)",
    },
    toolchains = ["@jq_toolchains//:resolved_toolchain"],
    visibility = ["//visibility:public"],
)
