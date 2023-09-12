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
        ":all_sampler_lib",
        "//envoy/registry",
        "//source/common/config:utility_lib",
    ],
)

envoy_cc_library(
    name = "all_sampler_lib",
    srcs = ["all_sampler.cc"],
    hdrs = ["all_sampler.h"],
    deps = [
        "//source/common/config:datasource_lib",
        "//source/extensions/tracers/opentelemetry/samplers:sampler_lib",
    ],
)