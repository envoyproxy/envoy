load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
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

envoy_cc_test_library(
    name = "integration_test_utils_lib",
    hdrs = ["integration_test_utils.h"],
    deps = [
        "//source/common/common:fmt_lib",
        "//test/config:utility_lib",
        "@envoy_api//contrib/envoy/extensions/reverse_tunnel_reporters/v3alpha/reporters:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/reverse_tunnel/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "grpc_client_integration_test",
    srcs = ["grpc_client_integration_test.cc"],
    deps = [
        ":integration_test_utils_lib",
        "//contrib/reverse_tunnel_reporter/source:config",
        "//source/common/thread_local:thread_local_lib",
        "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_connection_resolver_lib",
        "//source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface:reverse_tunnel_initiator_lib",
        "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:reverse_tunnel_acceptor_lib",
        "//source/extensions/filters/http/router:config",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/extensions/filters/network/reverse_tunnel:config",
        "//test/integration:integration_lib",
        "@com_github_grpc_grpc//:grpc++",
        "@envoy_api//contrib/envoy/extensions/reverse_tunnel_reporters/v3alpha/clients/grpc_client:pkg_cc_grpc",
        "@envoy_api//contrib/envoy/extensions/reverse_tunnel_reporters/v3alpha/clients/grpc_client:pkg_cc_proto",
        "@envoy_api//contrib/envoy/extensions/reverse_tunnel_reporters/v3alpha/reporters:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/http/v3:pkg_cc_proto",
    ],
)
