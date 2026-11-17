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
    extension_names = ["envoy.tracers.opentelemetry.samplers.always_on"],
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_large",
        },
    },
    deps = [
        "//envoy/registry",
        "//source/extensions/tracers/opentelemetry/samplers/always_on:always_on_sampler_lib",
        "//source/extensions/tracers/opentelemetry/samplers/always_on:config",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "always_on_sampler_test",
    srcs = ["always_on_sampler_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry.samplers.always_on"],
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_large",
        },
    },
    deps = [
        "//source/extensions/tracers/opentelemetry/samplers/always_on:always_on_sampler_lib",
        "//source/extensions/tracers/opentelemetry/samplers/always_on:config",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/tracers/opentelemetry/samplers/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "always_on_sampler_integration_test",
    srcs = [
        "always_on_sampler_integration_test.cc",
    ],
    extension_names = ["envoy.tracers.opentelemetry.samplers.always_on"],
    # linux_x64_xlarge: the coverage link peaks at 11314MB,
    # over linux_x64_large (12288MiB) with headroom.
    rbe_pool = {
        "gcc": {"build": "linux_x64_small"},
        "openssl": {"link": "linux_x64_small"},
        "coverage": {"link": "linux_x64_xlarge"},
        "tsan": {"link": "linux_x64_small"},
    },
    deps = [
        "//source/exe:main_common_lib",
        "//source/extensions/tracers/opentelemetry:config",
        "//source/extensions/tracers/opentelemetry/samplers/always_on:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)
