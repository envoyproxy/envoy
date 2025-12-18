load("@rules_foreign_cc//foreign_cc:configure.bzl", "configure_make")

licenses(["notice"])  # Apache 2

filegroup(
    name = "all",
    srcs = glob(["**"]),
    visibility = ["//visibility:public"],
)

configure_make(
    name = "openssl",
    lib_source = ":all",
    configure_in_place = True,
    configure_command = "Configure",
    configure_options = ["--libdir=lib"],
    targets = ["build_sw", "install_sw"],
    args = ["-j"],
    out_lib_dir = "lib",
    out_shared_libs = [
        "libssl.so.3",
        "libcrypto.so.3",
        "ossl-modules/legacy.so"
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
    srcs = [":libssl", ":libcrypto", ":legacy"],
    visibility = ["//visibility:public"],
)
