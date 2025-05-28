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
    deps = [
        "//envoy/registry",
        "//envoy/server:bootstrap_extension_config_interface",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "@envoy_api//envoy/extensions/filters/common/expr/v3:pkg_cc_proto",
    ],
) 