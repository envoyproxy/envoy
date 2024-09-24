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
    name = "file_sink_impl_test",
    srcs = ["file_sink_impl_test.cc"],
    extension_names = ["envoy.health_check.event_sinks.file"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/health_check/event_sinks/file:file_sink_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/server:health_checker_factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/health_check/event_sinks/file/v3:pkg_cc_proto",
    ],
)
