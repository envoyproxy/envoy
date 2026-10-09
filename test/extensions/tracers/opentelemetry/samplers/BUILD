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
    name = "sampler_test",
    srcs = ["sampler_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//envoy/registry",
        "//source/extensions/tracers/opentelemetry:opentelemetry_tracer_lib",
        "//source/extensions/tracers/opentelemetry/samplers:sampler_lib",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)
