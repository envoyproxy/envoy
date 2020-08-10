load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "http_tracer_impl_test",
    srcs = [
        "http_tracer_impl_test.cc",
    ],
    deps = [
        "//include/envoy/http:request_id_extension_interface",
        "//source/common/common:base64_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:request_id_extension_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/tracing:http_tracer_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/tracing/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "http_tracer_manager_impl_test",
    srcs = [
        "http_tracer_manager_impl_test.cc",
    ],
    deps = [
        "//source/common/tracing:http_tracer_config_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/common/tracing:http_tracer_manager_lib",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/server:tracer_factory_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/test_common:registry_lib",
    ],
)
