load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

COMMON_ENV = {
    "LDFLAGS": " ".join([
        "-L$$EXT_BUILD_ROOT/$(BINDIR)/bazel/external/lib",
        "-L$$EXT_BUILD_ROOT/$(BINDIR)/external/envoy/bazel/external/lib",
    ]),
}

envoy_cc_library(
    name = "compressor_lib",
    srcs = ["qatzip_compressor_impl.cc"],
    hdrs = ["qatzip_compressor_impl.h"],
    deps = [
        "//envoy/compression/compressor:compressor_interface",
        "@qatzip",
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
