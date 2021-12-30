load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    external_deps = ["hyperscan"],
    deps = [
        "//envoy/common:regex_interface",
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//contrib/envoy/extensions/hyperscan/v3alpha:pkg_cc_proto",
    ],
)
