load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//tools/base:envoy_python.bzl", "envoy_entry_point")
load("@envoy_repo//:path.bzl", "PATH")

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
    ],
    data = [
        ":extensions_build_config",
    ],
    pkg = "envoy.code.check",
)
