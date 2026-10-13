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
    tags = ["skip_on_windows"],
    deps = [
        ":cel_sampler_lib",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "@envoy_api//envoy/extensions/tracers/opentelemetry/samplers/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "cel_sampler_lib",
    srcs = ["cel_sampler.cc"],
    hdrs = ["cel_sampler.h"],
    deps = [
        "//source/common/config:datasource_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "//source/extensions/tracers/opentelemetry:opentelemetry_tracer_lib",
        "//source/extensions/tracers/opentelemetry/samplers:sampler_lib",
        "@envoy_api//envoy/extensions/tracers/opentelemetry/samplers/v3:pkg_cc_proto",
    ],
)
