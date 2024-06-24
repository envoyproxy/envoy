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
        ":always_on_sampler_lib",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/extensions/tracers/opentelemetry/samplers/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "always_on_sampler_lib",
    srcs = ["always_on_sampler.cc"],
    hdrs = ["always_on_sampler.h"],
    deps = [
        "//source/common/config:datasource_lib",
        "//source/extensions/tracers/opentelemetry:opentelemetry_tracer_lib",
        "//source/extensions/tracers/opentelemetry/samplers:sampler_lib",
    ],
)
