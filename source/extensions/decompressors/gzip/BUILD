licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "zlib_lib",
    srcs = ["zlib_decompressor.cc"],
    hdrs = ["zlib_decompressor.h"],
    external_deps = ["zlib"],
    deps = [
        "//include/envoy/decompressor:decompressor_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:zlib_base_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":zlib_lib",
        "//source/common/compressor:compressor_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/decompressors:factory_base_lib",
        "//source/extensions/filters/http:well_known_names",
        "@envoy_api//envoy/extensions/decompressors/gzip/v3:pkg_cc_proto",
    ],
)
