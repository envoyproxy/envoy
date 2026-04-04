load("@bazel_skylib//rules:common_settings.bzl", "string_flag")

config_setting(
    name = "windows_x86_64",
    values = {"cpu": "x64_windows"},
    visibility = ["//visibility:public"],
)

string_flag(
    name = "template-flavor",
    build_setting_default = "",
    visibility = ["//visibility:public"],
)
