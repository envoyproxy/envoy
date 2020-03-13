licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "tracer_test",
    srcs = ["tracer_test.cc"],
    extension_name = "envoy.tracers.opencensus",
    deps = [
        "//source/extensions/tracers/opencensus:opencensus_tracer_impl",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.tracers.opencensus",
    deps = [
        "//source/extensions/tracers/opencensus:config",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)
