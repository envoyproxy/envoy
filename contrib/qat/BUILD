load("//bazel:envoy_build_system.bzl", "envoy_contrib_package")
load("@rules_foreign_cc//foreign_cc:configure.bzl", "configure_make")
load(
    "//contrib:all_contrib_extensions.bzl",
    "envoy_contrib_linux_x86_64_constraints",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# yasm is a qatlib build-time tooling dependency. It's needed for compiling
# assembler for libcrc.

configure_make(
    name = "yasm_",
    autogen = True,
    configure_in_place = True,
    lib_source = "@com_github_yasm_yasm//:all",
    out_binaries = ["yasm"],
    target_compatible_with = envoy_contrib_linux_x86_64_constraints(),
    visibility = ["//visibility:public"],
)

# Filter out extra file paths from the :yasm_ target.

filegroup(
    name = "yasm_bin",
    srcs = [":yasm_"],
    output_group = "yasm",
)

# Have qatlib outside of any extensions because it may be used by both
# QAT compression library extension and QAT private key provider
# extension.

configure_make(
    name = "qatlib",
    autogen = True,
    build_data = [":yasm_bin"],
    configure_in_place = True,
    configure_options = [
        "--disable-systemd",
        "--disable-shared",
        "--with-pic",
        "--enable-static",
        "--enable-samples=no",
    ],
    env = {
        "YASM": "$(execpath :yasm_bin)",
    },
    lib_source = "@com_github_intel_qatlib//:all",
    out_static_libs = [
        "libqat.a",
        "libusdm.a",
    ],
    target_compatible_with = envoy_contrib_linux_x86_64_constraints(),
    visibility = ["//visibility:public"],
    deps = [
        "@boringssl//:ssl",
    ],
    alwayslink = True,
)
