load("@rules_foreign_cc//foreign_cc:configure.bzl", "configure_make")

licenses(["notice"])  # Apache 2

filegroup(
    name = "all",
    srcs = glob(["**"]),
    visibility = ["//visibility:public"],
)

configure_make(
    name = "openssl",
    args = select({
        "@envoy//bazel/foreign_cc:parallel_builds_enabled": ["-j"],
        "//conditions:default": ["-j1"],
    }),
    configure_command = "Configure",
    configure_in_place = True,
    configure_options = [
        "--libdir=lib",
        "enable-brotli",
        "--with-brotli-include=$$EXT_BUILD_DEPS/include",
        "--with-brotli-lib=$$EXT_BUILD_DEPS/lib",
        "enable-zlib",
        "--with-zlib-include=$$EXT_BUILD_DEPS/include",
        "--with-zlib-lib=$$EXT_BUILD_DEPS/lib",
    ] + select({
        "@envoy//bazel:dbg_build": ["--debug"],
        "//conditions:default": [],
    }),
    # Ensure OpenSSL will link in the brotli & zlib *.a (not *.so) files.
    configure_prefix = "rm -f $$EXT_BUILD_DEPS/lib/libbrotli*.so && " +
                       "ln -sf libzlib-ng.a $$EXT_BUILD_DEPS/lib/libz.a && ",
    env = {
        "CC": "$$EXT_BUILD_ROOT$$/$(CC)",
        "AR": "$$EXT_BUILD_ROOT$$/$(AR)",
    },
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
    deps = [
        "@brotli//:brotlicommon",
        "@brotli//:brotlidec",
        "@brotli//:brotlienc",
        "@zlib-ng//:zlib-ng",
    ],
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
