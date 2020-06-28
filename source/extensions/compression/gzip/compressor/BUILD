load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "compressor_lib",
    srcs = ["zlib_compressor_impl.cc"],
    hdrs = ["zlib_compressor_impl.h"],
    external_deps = ["zlib"],
    deps = [
        "//include/envoy/compression/compressor:compressor_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/extensions/compression/gzip/common:zlib_base_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":compressor_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/compression/common/compressor:compressor_factory_base_lib",
        "//source/extensions/filters/http:well_known_names",
        "@envoy_api//envoy/extensions/compression/gzip/compressor/v3:pkg_cc_proto",
    ],
)
