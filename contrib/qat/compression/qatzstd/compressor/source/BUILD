load("@rules_foreign_cc//foreign_cc:defs.bzl", "make")
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
    "ZSTDLIB": "$$EXT_BUILD_ROOT/external/zstd/lib",
    "QAT_INCLUDE_PATH": "$$EXT_BUILD_DEPS/qatlib/include/qat",
    "LDFLAGS": "-L$$EXT_BUILD_ROOT/$(BINDIR)/bazel/external/lib",
}

COMMON_CFLAGS = "-I$$EXT_BUILD_ROOT/external/numactl"

make(
    name = "qat-zstd",
    build_data = ["@com_github_qat_zstd//:all"],
    # Include the numa_archive genrule output so that libnuma.a is available
    # for the linker to find with -lnuma.
    data = ["//bazel/external:numa_archive"],
    env = select({
        "//bazel:clang_build": COMMON_ENV | {
            "CFLAGS": COMMON_CFLAGS + " -Wno-error=unused-parameter -Wno-error=unused-command-line-argument",
        },
        "//conditions:default": COMMON_ENV | {
            "CFLAGS": COMMON_CFLAGS,
        },
    }),
    includes = [],
    lib_source = "@com_github_qat_zstd//:all",
    out_static_libs = ["libqatseqprod.a"],
    tags = ["skip_on_windows"],
    target_compatible_with = envoy_contrib_linux_x86_64_constraints(),
    targets = [
        "install",
    ],
    deps = [
        "//contrib/qat:qatlib",
        "@zstd",
        # numactl headers are needed for compilation
        "@numactl//:numa",
    ],
)

envoy_cc_library(
    name = "compressor_lib",
    srcs = ["qatzstd_compressor_impl.cc"],
    hdrs = ["qatzstd_compressor_impl.h"],
    deps = [
        ":qat-zstd",
        "//envoy/compression/compressor:compressor_interface",
        "//envoy/server:factory_context_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/compression/zstd/common:zstd_base_lib",
        "//source/common/compression/zstd/compressor:compressor_base",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":compressor_lib",
        ":qat-zstd",
        "//envoy/event:dispatcher_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/http:headers_lib",
        "//source/extensions/compression/common/compressor:compressor_factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/compression/qatzstd/compressor/v3alpha:pkg_cc_proto",
    ],
)
