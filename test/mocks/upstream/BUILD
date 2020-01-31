licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

envoy_package()

envoy_cc_mock(
    name = "cluster_info_mocks",
    srcs = ["cluster_info.cc"],
    hdrs = ["cluster_info.h"],
    deps = [
        ":transport_socket_match_mocks",
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/config:metadata_lib",
        "//source/common/http:utility_lib",
        "//source/common/network:raw_buffer_socket_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_mock(
    name = "host_mocks",
    srcs = ["host.cc"],
    hdrs = ["host.h"],
    deps = [
        ":cluster_info_mocks",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/network:utility_lib",
        "//source/common/stats:stats_lib",
        "//test/mocks/network:transport_socket_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/cluster/v2alpha:pkg_cc_proto",
    ],
)

envoy_cc_mock(
    name = "transport_socket_match_mocks",
    srcs = ["transport_socket_match.cc"],
    hdrs = ["transport_socket_match.h"],
    deps = [
        "//include/envoy/upstream:upstream_interface",
        "//source/common/network:raw_buffer_socket_lib",
        "//source/common/stats:isolated_store_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_mock(
    name = "load_balancer_context_mock",
    srcs = ["load_balancer_context.cc"],
    hdrs = ["load_balancer_context.h"],
    deps = [
        "//include/envoy/upstream:load_balancer_interface",
    ],
)

envoy_cc_mock(
    name = "upstream_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        ":load_balancer_context_mock",
        ":transport_socket_match_mocks",
        "//include/envoy/http:async_client_interface",
        "//include/envoy/upstream:cluster_factory_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:health_checker_interface",
        "//include/envoy/upstream:load_balancer_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/upstream:cluster_factory_lib",
        "//source/common/upstream:health_discovery_service_lib",
        "//source/common/upstream:upstream_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/tcp:tcp_mocks",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/core/v3:pkg_cc_proto",
    ],
)
