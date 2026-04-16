load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:no_op",
        "//test/extensions/dynamic_modules/test_data/c:tracer_config_fail",
        "//test/extensions/dynamic_modules/test_data/c:tracer_no_op",
    ],
    deps = [
        "//source/common/stats:custom_stat_namespaces_lib",
        "//source/extensions/tracers/dynamic_modules:config",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/tracers/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "tracer_test",
    srcs = ["tracer_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:no_op",
        "//test/extensions/dynamic_modules/test_data/c:tracer_config_fail",
        "//test/extensions/dynamic_modules/test_data/c:tracer_no_op",
        "//test/extensions/dynamic_modules/test_data/c:tracer_with_values",
    ],
    deps = [
        "//source/extensions/tracers/dynamic_modules:tracer_config_lib",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "abi_impl_test",
    srcs = ["abi_impl_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:tracer_no_op",
    ],
    deps = [
        "//source/extensions/tracers/dynamic_modules:abi_impl",
        "//source/extensions/tracers/dynamic_modules:tracer_config_lib",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)
