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
    rbe_pool = "2core",
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
    srcs = [
        "dynatrace_sampler_test.cc",
        "sampler_config_provider_test.cc",
        "sampler_config_test.cc",
        "sampling_controller_test.cc",
        "stream_summary_test.cc",
        "tenant_id_test.cc",
    ],
    extension_names = ["envoy.tracers.opentelemetry.samplers.dynatrace"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/tracers/opentelemetry/samplers/dynatrace:dynatrace_sampler_lib",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/tracers/opentelemetry/samplers/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "dynatrace_sampler_integration_test",
    srcs = [
        "dynatrace_sampler_integration_test.cc",
    ],
    extension_names = ["envoy.tracers.opentelemetry.samplers.dynatrace"],
    rbe_pool = "2core",
    deps = [
        "//source/exe:main_common_lib",
        "//source/extensions/tracers/opentelemetry:config",
        "//source/extensions/tracers/opentelemetry/resource_detectors/dynatrace:config",
        "//source/extensions/tracers/opentelemetry/samplers/dynatrace:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)
