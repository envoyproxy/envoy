load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":sha1_lib",
        "//envoy/registry",
        "//source/extensions/hash:algorithm_provider_config_interface",
    ],
)

envoy_cc_library(
    name = "sha1_lib",
    srcs = ["algorithm_impl.cc"],
    hdrs = ["algorithm_impl.h"],
    external_deps = ["ssl"],
    deps = [
        "//source/extensions/hash:algorithm_provider_interface",
    ],
)
