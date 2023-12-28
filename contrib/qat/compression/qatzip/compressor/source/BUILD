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

configure_make(
    name = "qatzip",
    autogen = True,
    configure_in_place = True,
    configure_options = [
        "--enable-static",
        "--disable-shared",
    ],
    env = select({
        "//bazel:clang_build": {
            "CFLAGS": "-Wno-error=newline-eof -Wno-error=strict-prototypes -Wno-error=unused-but-set-variable",
        },
        "//conditions:default": {},
    }),
    lib_source = "@com_github_intel_qatzip//:all",
    out_static_libs = [
        "libqatzip.a",
    ],
    tags = ["skip_on_windows"],
    target_compatible_with = envoy_contrib_linux_x86_64_constraints(),
    visibility = ["//visibility:public"],
    deps = [
        "//bazel/foreign_cc:lz4",
        "//bazel/foreign_cc:zlib",
        "//contrib/qat:qatlib",
        "@boringssl//:ssl",
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
