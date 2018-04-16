licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_select_google_grpc",
)

envoy_package()

envoy_cc_test(
    name = "async_client_impl_test",
    srcs = ["async_client_impl_test.cc"],
    deps = [
        "//source/common/grpc:async_client_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/proto:helloworld_proto",
    ],
)

envoy_cc_test(
    name = "async_client_manager_impl_test",
    srcs = ["async_client_manager_impl_test.cc"],
    deps = [
        "//source/common/grpc:async_client_manager_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "codec_test",
    srcs = ["codec_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/grpc:codec_lib",
        "//test/proto:helloworld_proto",
    ],
)

envoy_cc_test(
    name = "common_test",
    srcs = ["common_test.cc"],
    deps = [
        "//source/common/grpc:common_lib",
        "//source/common/http:headers_lib",
        "//test/mocks/upstream:upstream_mocks",
        "//test/proto:helloworld_proto",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "google_async_client_impl_test",
    srcs = envoy_select_google_grpc(["google_async_client_impl_test.cc"]),
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/stats:stats_lib",
        "//source/common/tracing:http_tracer_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/proto:helloworld_proto",
    ] + envoy_select_google_grpc(["//source/common/grpc:google_async_client_lib"]),
)

envoy_cc_test_library(
    name = "grpc_client_integration_lib",
    hdrs = ["grpc_client_integration.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "grpc_client_integration_test",
    srcs = ["grpc_client_integration_test.cc"],
    data = ["//test/config/integration/certs"],
    deps = [
        ":grpc_client_integration_lib",
        "//source/common/common:enum_to_int",
        "//source/common/event:dispatcher_lib",
        "//source/common/grpc:async_client_lib",
        "//source/common/http/http2:conn_pool_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:raw_buffer_socket_lib",
        "//source/common/ssl:context_lib",
        "//source/common/ssl:context_config_lib",
        "//source/common/ssl:ssl_socket_lib",
        "//source/common/stats:stats_lib",
        "//test/integration:integration_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/proto:helloworld_proto",
        "//test/test_common:environment_lib",
    ] + envoy_select_google_grpc(["//source/common/grpc:google_async_client_lib"]),
)
