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
    extension_names = ["envoy.tracers.opentelemetry.samplers.cel"],
    rbe_pool = "6gig",
    tags = ["skip_on_windows"],
    deps = [
        "//envoy/registry",
        "//source/extensions/tracers/opentelemetry/samplers/cel:cel_sampler_lib",
        "//source/extensions/tracers/opentelemetry/samplers/cel:config",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "cel_sampler_test",
    srcs = ["cel_sampler_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry.samplers.cel"],
    rbe_pool = "6gig",
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/tracers/opentelemetry/samplers/cel:cel_sampler_lib",
        "//source/extensions/tracers/opentelemetry/samplers/cel:config",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/tracers/opentelemetry/samplers/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "cel_sampler_integration_test",
    srcs = [
        "cel_sampler_integration_test.cc",
    ],
    extension_names = ["envoy.tracers.opentelemetry.samplers.cel"],
    rbe_pool = "6gig",
    tags = ["skip_on_windows"],
    deps = [
        "//source/exe:main_common_lib",
        "//source/extensions/tracers/opentelemetry:config",
        "//source/extensions/tracers/opentelemetry/samplers/cel:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)
