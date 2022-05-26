load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_entry_point")
load("@envoy_repo//:path.bzl", "PATH")
load("@com_github_aignas_rules_shellcheck//:def.bzl", "shellcheck")
load("@aspect_bazel_lib//lib:jq.bzl", "jq")

licenses(["notice"])  # Apache 2

envoy_package()

jq(
    name = "extensions_build_config",
    srcs = [
        "//contrib:contrib_extensions_build_config",
        "//source/extensions:extensions_build_config",
    ],
    out = "extensions_build_config.json",
    args = ["--slurp"],
    filter = ".[0] * .[1]",
)

envoy_entry_point(
    name = "check",
    args = [
        "--extensions_build_config=$(location :extensions_build_config)",
        "--path=%s" % PATH,
        "-b shellcheck:$(location :shellcheck)",
    ],
    data = [
        ":extensions_build_config",
        ":shellcheck",
    ],
    pkg = "envoy.code.check",
)

shellcheck(name = "_shellcheck")

# Workaround for: https://github.com/aignas/rules_shellcheck/issues/5
sh_binary(
    name = "shellcheck",
    srcs = [":run_shellcheck.sh"],
    args = [
        "$(location :_shellcheck)",
    ],
    data = [":_shellcheck"],
)
