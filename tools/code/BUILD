load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_entry_point")
load("@envoy_repo//:path.bzl", "PATH")
load("@com_github_aignas_rules_shellcheck//:def.bzl", "shellcheck")

licenses(["notice"])  # Apache 2

envoy_package()

genrule(
    name = "extensions_build_config",
    outs = ["extensions_build_config.json"],
    cmd = """
    jq -s '.[0] * .[1]' \
        $(location //source/extensions:extensions_build_config) \
        $(location //contrib:contrib_extensions_build_config) \
            > $@
    """,
    tools = [
        "//contrib:contrib_extensions_build_config",
        "//source/extensions:extensions_build_config",
    ],
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
