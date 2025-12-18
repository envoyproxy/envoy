load("@rules_foreign_cc//foreign_cc:configure.bzl", "configure_make")

licenses(["notice"])  # Apache 2

filegroup(
    name = "all",
    srcs = glob(["**"]),
    visibility = ["//visibility:public"],
)

configure_make(
    name = "openssl",
    args = ["-j"],
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
