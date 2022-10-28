load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_entry_point")
load("@envoy_repo//:path.bzl", "PATH")
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
        "-b shellcheck:$(location @com_github_aignas_rules_shellcheck//:shellcheck)",
    ],
    data = [
        ":extensions_build_config",
        "@com_github_aignas_rules_shellcheck//:shellcheck",
    ],
    pkg = "envoy.code.check",
)
