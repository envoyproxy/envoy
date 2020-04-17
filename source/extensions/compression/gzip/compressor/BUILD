licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_package",
)

envoy_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        "//source/common/compressor:compressor_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/compression/common:compressor_factory_base_lib",
        "//source/extensions/filters/http:well_known_names",
        "@envoy_api//envoy/extensions/compression/gzip/compressor/v3:pkg_cc_proto",
    ],
)
