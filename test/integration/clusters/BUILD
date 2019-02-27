licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test_library(
    name = "custom_static_cluster",
    srcs = ["custom_static_cluster.cc"],
    deps = [
        "//include/envoy/api:api_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/upstream:cluster_factory_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/config:metadata_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
        "//source/common/singleton:manager_impl_lib",
        "//source/common/upstream:cluster_factory_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/server:transport_socket_config_lib",
        #        "//test/mocks:common_lib",
        #        "//test/mocks/local_info:local_info_mocks",
        #        "//test/mocks/network:network_mocks",
        #        "//test/mocks/runtime:runtime_mocks",
        #        "//test/mocks/server:server_mocks",
        #        "//test/mocks/ssl:ssl_mocks",
        #        "//test/mocks/upstream:upstream_mocks",
        "//test/proto:cluster_factory_config_proto_cc",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "//test/common/upstream:utility_lib",
    ],
)
