load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry.samplers.dynatrace"],
    deps = [
        "//envoy/registry",
        "//source/extensions/tracers/opentelemetry/samplers/dynatrace:config",
        "//source/extensions/tracers/opentelemetry/samplers/dynatrace:dynatrace_sampler_lib",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "dynatrace_sampler_test",
    srcs = ["tracestate_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry.samplers.dynatrace"],
    deps = [
        "//source/extensions/tracers/opentelemetry/samplers/dynatrace:dynatrace_sampler_lib",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/tracers/opentelemetry/samplers/v3:pkg_cc_proto",
    ],
)