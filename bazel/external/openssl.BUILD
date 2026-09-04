load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")
load("@rules_foreign_cc//foreign_cc:configure.bzl", "configure_make")

licenses(["notice"])  # Apache 2

filegroup(
    name = "all",
    srcs = glob(["**"]),
    visibility = ["//visibility:public"],
)

bool_flag(
    name = "parallel_builds",
    build_setting_default = False,
    visibility = ["//visibility:public"],
)

config_setting(
    name = "parallel_builds_enabled",
    flag_values = {
        ":parallel_builds": "True",
    },
)

configure_make(
    name = "openssl",
    args = select({
        ":parallel_builds_enabled": ["-j"],
        "//conditions:default": ["-j1"],
    }),
    configure_command = "Configure",
    configure_in_place = True,
    configure_options = ["--libdir=lib"],
    lib_source = ":all",
    out_lib_dir = "lib",
    out_shared_libs = [
        "libssl.so.3",
        "libcrypto.so.3",
        "ossl-modules/legacy.so",
    ],
    targets = [
        "build_sw",
        "install_sw",
    ],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "include",
    srcs = [":openssl"],
    output_group = "include",
    visibility = ["//visibility:public"],
)

filegroup(
    name = "libssl",
    srcs = [":openssl"],
    output_group = "libssl.so.3",
    visibility = ["//visibility:private"],
)

filegroup(
    name = "libcrypto",
    srcs = [":openssl"],
    output_group = "libcrypto.so.3",
    visibility = ["//visibility:private"],
)

filegroup(
    name = "legacy",
    srcs = [":openssl"],
    output_group = "legacy.so",
    visibility = ["//visibility:private"],
)

filegroup(
    name = "libs",
    srcs = [
        ":legacy",
        ":libcrypto",
        ":libssl",
    ],
    visibility = ["//visibility:public"],
)
