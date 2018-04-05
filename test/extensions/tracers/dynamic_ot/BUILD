licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "dynamic_opentracing_driver_impl_test",
    srcs = [
        "dynamic_opentracing_driver_impl_test.cc",
    ],
    data = [
        "@io_opentracing_cpp//mocktracer:libmocktracer_plugin.so",
    ],
    deps = [
        "//source/common/http:header_map_lib",
        "//source/extensions/tracers/dynamic_ot:dynamic_opentracing_driver_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/test_common:environment_lib",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    data = [
        "@io_opentracing_cpp//mocktracer:libmocktracer_plugin.so",
    ],
    deps = [
        "//source/extensions/tracers/dynamic_ot:config",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)
