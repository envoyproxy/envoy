load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "compressor_lib",
    srcs = ["brotli_compressor_impl.cc"],
    hdrs = ["brotli_compressor_impl.h"],
    deps = [
        "//envoy/compression/compressor:compressor_interface",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/compression/brotli/common:brotli_base_lib",
        "@org_brotli//:brotlienc",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":compressor_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/compression/common/compressor:compressor_factory_base_lib",
        "@envoy_api//envoy/extensions/compression/brotli/compressor/v3:pkg_cc_proto",
    ],
)
