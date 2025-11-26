load("@bazel_skylib//rules:common_settings.bzl", "string_flag")
load(":defs.bzl", "toolchain_enforcement")

licenses(["notice"])  # Apache 2

string_flag(
    name = "toolchain_identifier",
    build_setting_default = "",
    visibility = ["//visibility:public"],
)

toolchain_enforcement(
    name = "enforce_toolchain",
    toolchain_identifier = ":toolchain_identifier",
    visibility = ["//visibility:public"],
)
