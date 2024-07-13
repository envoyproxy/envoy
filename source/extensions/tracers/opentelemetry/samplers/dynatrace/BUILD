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
        ":dynatrace_sampler_lib",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/extensions/tracers/opentelemetry/samplers/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "dynatrace_sampler_lib",
    srcs = [
        "dynatrace_sampler.cc",
        "sampler_config.cc",
        "sampler_config_provider.cc",
        "sampling_controller.cc",
    ],
    hdrs = [
        "dynatrace_sampler.h",
        "sampler_config.h",
        "sampler_config_provider.h",
        "sampling_controller.h",
        "stream_summary.h",
        "tenant_id.h",
    ],
    deps = [
        "//source/common/config:datasource_lib",
        "//source/extensions/tracers/opentelemetry:opentelemetry_tracer_lib",
        "//source/extensions/tracers/opentelemetry/samplers:sampler_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/tracers/opentelemetry/samplers/v3:pkg_cc_proto",
    ],
)
