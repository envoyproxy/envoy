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
    name = "opentracing_driver_impl_test",
    srcs = [
        "opentracing_driver_impl_test.cc",
    ],
    extension_name = "envoy.tracers.dynamic_ot",
    # TODO(wrowe): envoy_extension_ rules don't currently exclude windows extensions
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/tracers/dynamic_ot:dynamic_opentracing_driver_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "@io_opentracing_cpp//mocktracer",
    ],
)
