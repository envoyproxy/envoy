load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    copts = [
        "-DUSE_CEL_PARSER",
    ],
    deps = [
        "//envoy/ratelimit:ratelimit_interface",
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "@cel-cpp//parser",
        "@envoy_api//envoy/extensions/rate_limit_descriptors/expr/v3:pkg_cc_proto",
    ],
)
