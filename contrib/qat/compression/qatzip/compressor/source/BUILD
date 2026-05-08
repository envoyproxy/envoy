load("@rules_foreign_cc//foreign_cc:configure.bzl", "configure_make")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)
load(
    "//contrib:all_contrib_extensions.bzl",
    "envoy_contrib_linux_x86_64_constraints",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

COMMON_ENV = {
    "LDFLAGS": " ".join([
        "-L$$EXT_BUILD_ROOT/$(BINDIR)/bazel/external/lib",
        "-L$$EXT_BUILD_ROOT/$(BINDIR)/external/envoy/bazel/external/lib",
    ]),
}

configure_make(
    name = "qatzip",
    autogen = True,
    configure_in_place = True,
    configure_options = [
        "--enable-static",
        "--disable-shared",
    ],
    # Include the numa_archive genrule output so that libnuma.a is available
    # for the linker to find with -lnuma. Include zlib_ng_archive when building
    # with zlib-ng so that libz.a is available for the configure script.
    data = [
        "//bazel/external:numa_archive",
        "//bazel/external:zlib_archive",
    ],
    # Point to the directory containing the libnuma.a and libz.a archives.
    # The cc_library targets don't produce files that can be found with -lnuma
    # or -lz, so we need to use the genrule outputs.
    env = select({
        "//bazel:clang_build": COMMON_ENV | {
            "CFLAGS": "-Wno-error=newline-eof -Wno-error=strict-prototypes -Wno-error=unused-but-set-variable",
        },
        "//conditions:default": COMMON_ENV,
    }),
    lib_source = "@qatzip//:all",
    out_static_libs = [
        "libqatzip.a",
    ],
    tags = ["skip_on_windows"],
    target_compatible_with = envoy_contrib_linux_x86_64_constraints(),
    visibility = ["//visibility:public"],
    deps = [
        "//bazel/foreign_cc:lz4",
        "//bazel:zlib",
        "//contrib/qat:qatlib",
        # Use crypto label_flag to select the SSL library.
        "//bazel:crypto",
        "@numactl//:numa",
    ],
    alwayslink = False,
)

envoy_cc_library(
    name = "compressor_lib",
    srcs = ["qatzip_compressor_impl.cc"],
    hdrs = ["qatzip_compressor_impl.h"],
    deps = [
        ":qatzip",
        "//envoy/compression/compressor:compressor_interface",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    defines = select({
        "//bazel:linux_x86_64": [
        ],
        "//conditions:default": [
            "QAT_DISABLED=1",
        ],
    }),
    deps = [
        "//envoy/compression/compressor:compressor_config_interface",
        "//envoy/compression/compressor:compressor_factory_interface",
        "//source/common/http:headers_lib",
        "@envoy_api//contrib/envoy/extensions/compression/qatzip/compressor/v3alpha:pkg_cc_proto",
    ] + select({
        "//bazel:linux_x86_64": [
            ":compressor_lib",
        ],
        "//conditions:default": [
        ],
    }),
)
