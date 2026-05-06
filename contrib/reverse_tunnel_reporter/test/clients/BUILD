load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "grpc_client_test",
    srcs = ["grpc_client_test.cc"],
    deps = [
        "//contrib/reverse_tunnel_reporter/source/clients/grpc_client:grpc_client_lib",
        "//envoy/grpc:async_client_interface",
        "//envoy/registry",
        "//source/common/api:api_lib",
        "//source/common/protobuf:utility_lib_header",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:thread_local_cluster_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)
