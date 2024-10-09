load("@rules_foreign_cc//foreign_cc:defs.bzl", "make")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

make(
    name = "qat-zstd",
    build_data = ["@com_github_qat_zstd//:all"],
    env = select({
        "//bazel:clang_build": {
            "CFLAGS": "-Wno-error=unused-parameter -Wno-error=unused-command-line-argument -I$$EXT_BUILD_DEPS/qatlib/include -I$$EXT_BUILD_DEPS/zstd/include",
        },
        "//conditions:default": {
            "CFLAGS": "-I$$EXT_BUILD_DEPS/qatlib/include -I$$EXT_BUILD_DEPS/zstd/include",
        },
    }),
    includes = [],
    lib_source = "@com_github_qat_zstd//:all",
    out_static_libs = ["libqatseqprod.a"],
    tags = ["skip_on_windows"],
    target_compatible_with = [
        "@platforms//os:linux",
        "@platforms//cpu:x86_64",
    ],
    targets = [
        "ENABLE_USDM_DRV=1",
        "install",
    ],
    deps = [
        "//bazel/foreign_cc:zstd",
        "//contrib/qat:qatlib",
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
