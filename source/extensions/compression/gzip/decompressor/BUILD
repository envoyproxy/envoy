load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "zlib_decompressor_impl_lib",
    srcs = ["zlib_decompressor_impl.cc"],
    hdrs = ["zlib_decompressor_impl.h"],
    external_deps = ["zlib"],
    deps = [
        "//include/envoy/compression/decompressor:decompressor_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/extensions/compression/gzip/common:zlib_base_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":zlib_decompressor_impl_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/compression/common/decompressor:decompressor_factory_base_lib",
        "@envoy_api//envoy/extensions/compression/gzip/decompressor/v3:pkg_cc_proto",
    ],
)
