load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "igzip_compressor_lib",
    srcs = ["igzip_compressor_impl.cc"],
    hdrs = ["igzip_compressor_impl.h"],
    external_deps = ["zlib"],
    deps = [
        "//envoy/compression/compressor:compressor_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//contrib/isa_l/compression/source/common:igzip_base_lib",
        "@envoy//bazel/foreign_cc:isa_l",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":igzip_compressor_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/compression/common/compressor:compressor_factory_base_lib",
        "@envoy_api//envoy/extensions/compression/compressor/igzip/v3alpha:pkg_cc_proto",
    ],
)
