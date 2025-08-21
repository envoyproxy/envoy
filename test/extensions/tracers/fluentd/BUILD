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
    name = "tracer_impl_test",
    srcs = ["tracer_impl_test.cc"],
    extension_names = ["envoy.tracers.fluentd"],
    rbe_pool = "2core",
    # TODO(wrowe): envoy_extension_ rules don't currently exclude windows extensions
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/tracers/fluentd:config",
        "//source/extensions/tracers/fluentd:fluentd_tracer_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@com_github_msgpack_cpp//:msgpack",
    ],
)

envoy_extension_cc_test(
    name = "tracer_integration_test",
    srcs = ["tracer_integration_test.cc"],
    extension_names = ["envoy.tracers.fluentd"],
    rbe_pool = "2core",
    # TODO(wrowe): envoy_extension_ rules don't currently exclude windows extensions
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/common:base64_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/tracers/fluentd:config",
        "//source/extensions/tracers/fluentd:fluentd_tracer_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/mocks/server:tracer_factory_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:thread_local_cluster_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.tracers.fluentd"],
    rbe_pool = "2core",
    # TODO(wrowe): envoy_extension_ rules don't currently exclude windows extensions
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/tracers/fluentd:config",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/mocks/server:tracer_factory_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)
