load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])

envoy_package()

envoy_extension_cc_test(
    name = "file_sink_impl_test",
    srcs = ["file_sink_impl_test.cc"],
    extension_names = ["envoy.health_check_event_sinks.file"],
    deps = [
        "//source/extensions/access_loggers/file:config",
        "//test/mocks/server:health_checker_factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "//source/extensions/health_check_event_sinks/file:file_sink_lib",
        "@envoy_api//envoy/extensions/health_check_event_sinks/file/v3:pkg_cc_proto",
    ],

